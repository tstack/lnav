/**
 * Copyright (c) 2007-2012, Timothy Stack
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "config.h"

#if defined HAVE_NCURSESW_CURSES_H
#    include <ncursesw/curses.h>
#elif defined HAVE_NCURSESW_H
#    include <ncursesw.h>
#elif defined HAVE_NCURSES_CURSES_H
#    include <ncurses/curses.h>
#elif defined HAVE_NCURSES_H
#    include <ncurses.h>
#elif defined HAVE_CURSES_H
#    include <curses.h>
#else
#    error "SysV or X/Open-compatible Curses header file required"
#endif

#ifdef HAVE_PTY_H
#    include <pty.h>
#endif

#ifdef HAVE_UTIL_H
#    include <util.h>
#endif

#ifdef HAVE_LIBUTIL_H
#    include <libutil.h>
#endif

#include <algorithm>
#include <filesystem>
#include <map>
#include <queue>
#include <sstream>
#include <string>
#include <utility>

#include "base/auto_fd.hh"
#include "base/auto_mem.hh"
#include "base/string_util.hh"
#include "fmt/format.h"
#include "styling.hh"
#include "termios_guard.hh"
#include "ww898/cp_utf8.hpp"

using namespace std;

/**
 * An RAII class for opening a PTY and forking a child process.
 */
class child_term {
public:
    class error : public std::exception {
    public:
        error(int err) : e_err(err){};

        int e_err;
    };

    explicit child_term(bool passin)
    {
        struct winsize ws;
        auto_fd slave;

        memset(&ws, 0, sizeof(ws));

        if (isatty(STDIN_FILENO)
            && tcgetattr(STDIN_FILENO, &this->ct_termios) == -1)
        {
            throw error(errno);
        }

        if (isatty(STDOUT_FILENO)
            && ioctl(STDOUT_FILENO, TIOCGWINSZ, &this->ct_winsize) == -1)
        {
            throw error(errno);
        }

        ws.ws_col = 80;
        ws.ws_row = 24;

        if (openpty(this->ct_master.out(), slave.out(), nullptr, nullptr, &ws)
            < 0)
        {
            throw error(errno);
        }

        if ((this->ct_child = fork()) == -1)
            throw error(errno);

        if (this->ct_child == 0) {
            this->ct_master.reset();

            if (!passin) {
                dup2(slave, STDIN_FILENO);
            }
            dup2(slave, STDOUT_FILENO);

            setenv("TERM", "xterm-color", 1);
        } else {
            slave.reset();
        }
    };

    virtual ~child_term()
    {
        (void) this->wait_for_child();

        if (isatty(STDIN_FILENO)
            && tcsetattr(STDIN_FILENO, TCSANOW, &this->ct_termios) == -1)
        {
            perror("tcsetattr");
        }
        if (isatty(STDOUT_FILENO)
            && ioctl(STDOUT_FILENO, TIOCSWINSZ, &this->ct_winsize) == -1)
        {
            perror("ioctl");
        }
    };

    int wait_for_child()
    {
        int retval = -1;

        if (this->ct_child > 0) {
            kill(this->ct_child, SIGTERM);
            this->ct_child = -1;

            while (wait(&retval) < 0 && (errno == EINTR))
                ;
        }

        return retval;
    };

    bool is_child() const { return this->ct_child == 0; };

    pid_t get_child_pid() const { return this->ct_child; };

    int get_fd() const { return this->ct_master; };

protected:
    pid_t ct_child;
    auto_fd ct_master;
    struct termios ct_termios;
    struct winsize ct_winsize;
};

/**
 * @param fd The file descriptor to switch to raw mode.
 * @return Zero on success, -1 on error.
 */
static int
tty_raw(int fd)
{
    struct termios attr[1];

    assert(fd >= 0);

    if (tcgetattr(fd, attr) == -1)
        return -1;

    attr->c_lflag &= ~(ECHO | ICANON | IEXTEN);
    attr->c_iflag &= ~(ICRNL | INPCK | ISTRIP | IXON);
    attr->c_cflag &= ~(CSIZE | PARENB);
    attr->c_cflag |= (CS8);
    attr->c_oflag &= ~(OPOST);
    attr->c_cc[VMIN] = 1;
    attr->c_cc[VTIME] = 0;

    return tcsetattr(fd, TCSANOW, attr);
}

static void
dump_memory(FILE* dst, const char* src, int len)
{
    int lpc;

    for (lpc = 0; lpc < len; lpc++) {
        fprintf(dst, "%02x", src[lpc] & 0xff);
    }
}

static std::vector<char>
hex2bits(const char* src)
{
    std::vector<char> retval;

    for (size_t lpc = 0; src[lpc] && isdigit(src[lpc]); lpc += 2) {
        int val;

        sscanf(&src[lpc], "%2x", &val);
        retval.push_back((char) val);
    }

    return retval;
}

static const char*
tstamp()
{
    static char buf[64];

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.", localtime(&tv.tv_sec));
    auto dlen = strlen(buf);
    snprintf(&buf[dlen], sizeof(buf) - dlen, "%.06d", tv.tv_usec);

    return buf;
}

typedef enum {
    CT_WRITE,
} command_type_t;

struct command {
    command_type_t c_type;
    vector<char> c_arg;
};

static struct {
    const char* sd_program_name{nullptr};
    sig_atomic_t sd_looping{true};

    pid_t sd_child_pid{-1};

    std::filesystem::path sd_actual_name;
    auto_mem<FILE> sd_from_child{fclose};
    std::filesystem::path sd_expected_name;

    deque<struct command> sd_replay;
} scripty_data;

static const std::map<std::string, std::string> CSI_TO_DESC = {
    {")0", "Use alt charset"},

    {"[?1000l", "Don't Send Mouse X & Y"},
    {"[?1002l", "Don’t Use Cell Motion Mouse Tracking"},
    {"[?1006l", "Don't ..."},
    {"[?1h", "Application cursor keys"},
    {"[?1l", "Normal cursor keys"},
    {"[?47h", "Use alternate screen buffer"},
    {"[?47l", "Use normal screen buffer"},
    {"[2h", "Set Keyboard Action mode"},
    {"[4h", "Set Replace mode"},
    {"[12h", "Set Send/Receive mode"},
    {"[20h", "Set Normal Linefeed mode"},
    {"[2l", "Reset Keyboard Action mode"},
    {"[4l", "Reset Replace mode"},
    {"[12l", "Reset Send/Receive mode"},
    {"[20l", "Reset Normal Linefeed mode"},
    {"[2J", "Erase all"},
};

struct term_machine {
    enum class state {
        NORMAL,
        ESCAPE_START,
        ESCAPE_FIXED_LENGTH,
        ESCAPE_VARIABLE_LENGTH,
        ESCAPE_OSC,
    };

    struct term_attr {
        term_attr(size_t pos, const std::string& desc)
            : ta_pos(pos), ta_end(pos), ta_desc({desc})
        {
        }

        term_attr(size_t pos, size_t end, const std::string& desc)
            : ta_pos(pos), ta_end(end), ta_desc({desc})
        {
        }

        size_t ta_pos;
        size_t ta_end;
        std::vector<std::string> ta_desc;
    };

    term_machine(child_term& ct) : tm_child_term(ct) { this->clear(); }

    ~term_machine() { this->flush_line(); }

    void clear()
    {
        std::fill(begin(this->tm_line), end(this->tm_line), ' ');
        this->tm_line_attrs.clear();
        this->tm_new_data = false;
    }

    void add_line_attr(const std::string& desc)
    {
        if (!this->tm_line_attrs.empty()
            && this->tm_line_attrs.back().ta_pos == this->tm_cursor_x)
        {
            this->tm_line_attrs.back().ta_desc.emplace_back(desc);
        } else {
            this->tm_line_attrs.emplace_back(this->tm_cursor_x, desc);
        }
    }

    void write_char(char ch)
    {
        if (isprint(ch)) {
            require(ch);

            this->tm_new_data = true;
            this->tm_line[this->tm_cursor_x++] = (unsigned char) ch;
        } else {
            switch (ch) {
                case '\a':
                    this->flush_line();
                    fprintf(scripty_data.sd_from_child, "CTRL bell\n");
                    break;
                case '\x08':
                    this->add_line_attr("backspace");
                    if (this->tm_cursor_x > 0) {
                        this->tm_cursor_x -= 1;
                    }
                    break;
                case '\r':
                    this->add_line_attr("carriage-return");
                    this->tm_cursor_x = 0;
                    break;
                case '\n':
                    this->flush_line();
                    if (this->tm_cursor_y >= 0) {
                        this->tm_cursor_y += 1;
                    }
                    this->tm_cursor_x = 0;
                    break;
                case '\x0e':
                    this->tm_shift_start = this->tm_cursor_x;
                    break;
                case '\x0f':
                    if (this->tm_shift_start != this->tm_cursor_x) {
                        this->tm_line_attrs.emplace_back(
                            this->tm_shift_start, this->tm_cursor_x, "alt");
                    }
                    break;
                default:
                    require(ch);
                    this->tm_new_data = true;
                    this->tm_line[this->tm_cursor_x++] = (unsigned char) ch;
                    break;
            }
        }
    }

    void flush_line()
    {
        if (std::exchange(this->tm_waiting_on_input, false)
            && !this->tm_user_input.empty())
        {
            fprintf(stderr, "%s:flush keys\n", tstamp());
            fprintf(scripty_data.sd_from_child, "K ");
            dump_memory(
                scripty_data.sd_from_child, this->tm_user_input.data(), 1);
            fprintf(scripty_data.sd_from_child, "\n");
            this->tm_user_input.erase(this->tm_user_input.begin());
        }
        if (this->tm_new_data || !this->tm_line_attrs.empty()) {
            // fprintf(scripty_data.sd_from_child, "flush %d\n",
            // this->tm_flush_count);
            fprintf(stderr, "%s:flush %zu\n", tstamp(), this->tm_flush_count++);
            fprintf(
                scripty_data.sd_from_child, "S % 3d \u250B", this->tm_cursor_y);
            for (auto uch : this->tm_line) {
                ww898::utf::utf8::write(uch, [](auto ch) {
                    fputc(ch, scripty_data.sd_from_child);
                });
            }
            fprintf(scripty_data.sd_from_child, "\u250B\n");
            for (size_t lpc = 0; lpc < this->tm_line_attrs.size(); lpc++) {
                const auto& ta = this->tm_line_attrs[lpc];
                auto full_desc = fmt::format(
                    "{}",
                    fmt::join(ta.ta_desc.begin(), ta.ta_desc.end(), ", "));
                int line_len;

                if (ta.ta_pos == ta.ta_end) {
                    line_len = fprintf(
                        scripty_data.sd_from_child,
                        "A      %s%s %s",
                        repeat("\u00B7", ta.ta_pos).c_str(),
                        ((lpc + 1 < this->tm_line_attrs.size())
                         && (ta.ta_pos == this->tm_line_attrs[lpc + 1].ta_pos))
                            ? "\u251C"
                            : "\u2514",
                        full_desc.c_str());
                    line_len -= 2 + ta.ta_pos;
                } else {
                    line_len = fprintf(
                        scripty_data.sd_from_child,
                        "A      %s%s%s\u251b %s",
                        std::string(ta.ta_pos, ' ').c_str(),
                        ((lpc + 1 < this->tm_line_attrs.size())
                         && (ta.ta_pos == this->tm_line_attrs[lpc + 1].ta_pos))
                            ? "\u2518"
                            : "\u2514",
                        std::string(ta.ta_end - ta.ta_pos - 1, '-').c_str(),
                        full_desc.c_str());
                    line_len -= 4;
                }
                for (size_t lpc2 = lpc + 1; lpc2 < this->tm_line_attrs.size();
                     lpc2++)
                {
                    auto bar_pos = 7 + this->tm_line_attrs[lpc2].ta_pos;

                    if (bar_pos < line_len) {
                        continue;
                    }
                    line_len += fprintf(
                        scripty_data.sd_from_child,
                        "%s\u2502",
                        std::string(bar_pos - line_len, ' ').c_str());
                    line_len -= 2;
                }
                fprintf(scripty_data.sd_from_child, "\n");
            }
            this->clear();
        }
        fflush(scripty_data.sd_from_child);
    }

    std::vector<int> get_m_params()
    {
        std::vector<int> retval;
        size_t index = 1;

        while (index < this->tm_escape_buffer.size()) {
            int val, last;

            if (sscanf(&this->tm_escape_buffer[index], "%d%n", &val, &last)
                == 1)
            {
                retval.push_back(val);
                index += last;
                if (this->tm_escape_buffer[index] != ';') {
                    break;
                }
                index += 1;
            } else {
                break;
            }
        }

        return retval;
    }

    void new_user_input(char ch) { this->tm_user_input.push_back(ch); }

    void new_input(char ch)
    {
        if (this->tm_unicode_remaining > 0) {
            this->tm_unicode_buffer.push_back(ch);
            this->tm_unicode_remaining -= 1;
            if (this->tm_unicode_remaining == 0) {
                this->tm_new_data = true;
                this->tm_line[this->tm_cursor_x++]
                    = ww898::utf::utf8::read([this]() {
                          auto retval = this->tm_unicode_buffer.front();

                          this->tm_unicode_buffer.pop_front();
                          return retval;
                      }).unwrap();
            }
            return;
        } else {
            auto utfsize = ww898::utf::utf8::char_size(
                [ch]() { return std::make_pair(ch, 16); });

            if (utfsize.unwrap() > 1) {
                this->tm_unicode_remaining = utfsize.unwrap() - 1;
                this->tm_unicode_buffer.push_back(ch);
                return;
            }
        }

        switch (this->tm_state) {
            case state::NORMAL: {
                switch (ch) {
                    case '\x1b': {
                        this->tm_escape_buffer.clear();
                        this->tm_state = state::ESCAPE_START;
                        break;
                    }
                    default: {
                        this->write_char(ch);
                        break;
                    }
                }
                break;
            }
            case state::ESCAPE_START: {
                switch (ch) {
                    case '[': {
                        this->tm_escape_buffer.push_back(ch);
                        this->tm_state = state::ESCAPE_VARIABLE_LENGTH;
                        break;
                    }
                    case ']': {
                        this->tm_escape_buffer.push_back(ch);
                        this->tm_state = state::ESCAPE_OSC;
                        break;
                    }
                    case '(':
                    case ')':
                    case '*':
                    case '+': {
                        this->tm_state = state::ESCAPE_FIXED_LENGTH;
                        this->tm_escape_buffer.push_back(ch);
                        this->tm_escape_expected_size = 2;
                        break;
                    }
                    default: {
                        this->flush_line();
                        switch (ch) {
                            case '7':
                                fprintf(scripty_data.sd_from_child,
                                        "CTRL save cursor\n");
                                break;
                            case '8':
                                fprintf(scripty_data.sd_from_child,
                                        "CTRL restore cursor\n");
                                break;
                            case '>':
                                fprintf(scripty_data.sd_from_child,
                                        "CTRL Normal keypad\n");
                                break;
                            default: {
                                fprintf(scripty_data.sd_from_child,
                                        "CTRL %c\n",
                                        ch);
                                break;
                            }
                        }
                        this->tm_state = state::NORMAL;
                        break;
                    }
                }
                break;
            }
            case state::ESCAPE_FIXED_LENGTH: {
                this->tm_escape_buffer.push_back(ch);
                if (this->tm_escape_buffer.size()
                    == this->tm_escape_expected_size)
                {
                    auto iter = CSI_TO_DESC.find(
                        std::string(this->tm_escape_buffer.data(),
                                    this->tm_escape_buffer.size()));
                    this->flush_line();
                    if (iter == CSI_TO_DESC.end()) {
                        fprintf(scripty_data.sd_from_child,
                                "CTRL %.*s\n",
                                (int) this->tm_escape_buffer.size(),
                                this->tm_escape_buffer.data());
                    } else {
                        fprintf(scripty_data.sd_from_child,
                                "CTRL %s\n",
                                iter->second.c_str());
                    }
                    this->tm_state = state::NORMAL;
                }
                break;
            }
            case state::ESCAPE_VARIABLE_LENGTH: {
                this->tm_escape_buffer.push_back(ch);
                if (isalpha(ch)) {
                    auto iter = CSI_TO_DESC.find(
                        std::string(this->tm_escape_buffer.data(),
                                    this->tm_escape_buffer.size()));
                    if (iter == CSI_TO_DESC.end()) {
                        this->tm_escape_buffer.push_back('\0');
                        switch (ch) {
                            case 'A': {
                                auto amount = this->get_m_params();
                                int count = 1;

                                if (!amount.empty()) {
                                    count = amount[0];
                                }
                                this->flush_line();
                                this->tm_cursor_y -= count;
                                if (this->tm_cursor_y < 0) {
                                    this->tm_cursor_y = 0;
                                }
                                break;
                            }
                            case 'B': {
                                auto amount = this->get_m_params();
                                int count = 1;

                                if (!amount.empty()) {
                                    count = amount[0];
                                }
                                this->flush_line();
                                this->tm_cursor_y += count;
                                break;
                            }
                            case 'C': {
                                auto amount = this->get_m_params();
                                int count = 1;

                                if (!amount.empty()) {
                                    count = amount[0];
                                }
                                this->tm_cursor_x += count;
                                break;
                            }
                            case 'J': {
                                auto param = this->get_m_params();

                                this->flush_line();

                                auto region = param.empty() ? 0 : param[0];
                                switch (region) {
                                    case 0:
                                        fprintf(scripty_data.sd_from_child,
                                                "CSI Erase Below\n");
                                        break;
                                    case 1:
                                        fprintf(scripty_data.sd_from_child,
                                                "CSI Erase Above\n");
                                        break;
                                    case 2:
                                        fprintf(scripty_data.sd_from_child,
                                                "CSI Erase All\n");
                                        break;
                                    case 3:
                                        fprintf(scripty_data.sd_from_child,
                                                "CSI Erase Saved Lines\n");
                                        break;
                                }
                                break;
                            }
                            case 'K': {
                                auto param = this->get_m_params();

                                this->flush_line();

                                auto region = param.empty() ? 0 : param[0];
                                switch (region) {
                                    case 0:
                                        fprintf(scripty_data.sd_from_child,
                                                "CSI Erase to Right\n");
                                        break;
                                    case 1:
                                        fprintf(scripty_data.sd_from_child,
                                                "CSI Erase to Left\n");
                                        break;
                                    case 2:
                                        fprintf(scripty_data.sd_from_child,
                                                "CSI Erase All\n");
                                        break;
                                }
                                break;
                            }
                            case 'H': {
                                auto coords = this->get_m_params();

                                if (coords.empty()) {
                                    coords = {1, 1};
                                }
                                this->flush_line();
                                this->tm_cursor_y = coords[0];
                                this->tm_cursor_x = coords[1] - 1;
                                break;
                            }
                            case 'r': {
                                auto region = this->get_m_params();

                                this->flush_line();
                                fprintf(scripty_data.sd_from_child,
                                        "CSI set scrolling region %d-%d\n",
                                        region[0],
                                        region[1]);
                                break;
                            }
                            case 'm': {
                                auto attrs = this->get_m_params();

                                if (attrs.empty()) {
                                    this->add_line_attr("normal");
                                } else if ((30 <= attrs[0]) && (attrs[0] <= 37))
                                {
                                    auto xt = xterm_colors();

                                    this->add_line_attr(fmt::format(
                                        "fg({})",
                                        xt->tc_palette[attrs[0] - 30].xc_hex));
                                } else if (attrs[0] == 38) {
                                    auto xt = xterm_colors();

                                    require(attrs[1] == 5);
                                    this->add_line_attr(fmt::format(
                                        "fg({})",
                                        xt->tc_palette[attrs[2]].xc_hex));
                                } else if ((40 <= attrs[0]) && (attrs[0] <= 47))
                                {
                                    auto xt = xterm_colors();

                                    this->add_line_attr(fmt::format(
                                        "bg({})",
                                        xt->tc_palette[attrs[0] - 40].xc_hex));
                                } else if (attrs[0] == 48) {
                                    auto xt = xterm_colors();

                                    require(attrs[1] == 5);
                                    this->add_line_attr(fmt::format(
                                        "bg({})",
                                        xt->tc_palette[attrs[2]].xc_hex));
                                } else {
                                    switch (attrs[0]) {
                                        case 1:
                                            this->add_line_attr("bold");
                                            break;
                                        case 4:
                                            this->add_line_attr("underline");
                                            break;
                                        case 5:
                                            this->add_line_attr("blink");
                                            break;
                                        case 7:
                                            this->add_line_attr("inverse");
                                            break;
                                        default:
                                            this->add_line_attr(
                                                this->tm_escape_buffer.data());
                                            break;
                                    }
                                }
                                break;
                            }
                            default:
                                fprintf(stderr, "%s:missed %c\n", tstamp(), ch);
                                this->add_line_attr(
                                    this->tm_escape_buffer.data());
                                break;
                        }
                    } else {
                        this->flush_line();
                        fprintf(scripty_data.sd_from_child,
                                "CSI %s\n",
                                iter->second.c_str());
                    }
                    this->tm_state = state::NORMAL;
                } else {
                }
                break;
            }
            case state::ESCAPE_OSC: {
                if (ch == '\a') {
                    this->tm_escape_buffer.push_back('\0');

                    auto num = this->get_m_params();
                    auto semi_index
                        = strchr(this->tm_escape_buffer.data(), ';');

                    switch (num[0]) {
                        case 0: {
                            this->flush_line();
                            fprintf(scripty_data.sd_from_child,
                                    "OSC Set window title: %s\n",
                                    semi_index + 1);
                            break;
                        }
                        case 999: {
                            this->flush_line();
                            this->tm_waiting_on_input = true;
                            if (!scripty_data.sd_replay.empty()) {
                                const auto& cmd
                                    = scripty_data.sd_replay.front();

                                this->tm_user_input = cmd.c_arg;
                                write(this->tm_child_term.get_fd(),
                                      this->tm_user_input.data(),
                                      this->tm_user_input.size());

                                scripty_data.sd_replay.pop_front();
                            }
                            break;
                        }
                    }

                    this->tm_state = state::NORMAL;
                } else {
                    this->tm_escape_buffer.push_back(ch);
                }
                break;
            }
        }
    }

    child_term& tm_child_term;
    bool tm_waiting_on_input{false};
    state tm_state{state::NORMAL};
    std::vector<char> tm_escape_buffer;
    std::deque<uint8_t> tm_unicode_buffer;
    size_t tm_unicode_remaining{0};
    size_t tm_escape_expected_size{0};
    uint32_t tm_line[80];
    bool tm_new_data{false};
    size_t tm_cursor_x{0};
    int tm_cursor_y{-1};
    size_t tm_shift_start{0};
    std::vector<term_attr> tm_line_attrs;

    std::vector<char> tm_user_input;

    size_t tm_flush_count{0};
};

static void
sigchld(int sig)
{
}

static void
sigpass(int sig)
{
    kill(scripty_data.sd_child_pid, sig);
}

static void
usage()
{
    const char* usage_msg
        = "usage: %s [-h] [-t to_child] [-f from_child] -- <cmd>\n"
          "\n"
          "Recorder for TTY I/O from a child process."
          "\n"
          "Options:\n"
          "  -h         Print this message, then exit.\n"
          "  -n         Do not pass the output to the console.\n"
          "  -i         Pass stdin to the child process instead of connecting\n"
          "             the child to the tty.\n"
          "  -a <file>  The file where the actual I/O from/to the child "
          "process\n"
          "             should be stored.\n"
          "  -e <file>  The file containing the expected I/O from/to the "
          "child\n"
          "             process.\n"
          "\n"
          "Examples:\n"
          "  To record a session for playback later:\n"
          "    $ scripty -a output.0 -- myCursesApp\n"
          "\n"
          "  To replay the recorded session:\n"
          "    $ scripty -e input.0 -- myCursesApp\n";

    fprintf(stderr, usage_msg, scripty_data.sd_program_name);
}

int
main(int argc, char* argv[])
{
    int c, fd, retval = EXIT_SUCCESS;
    bool passout = true, passin = false, prompt = false;
    auto_mem<FILE> file(fclose);

    scripty_data.sd_program_name = argv[0];
    scripty_data.sd_looping = true;

    while ((c = getopt(argc, argv, "ha:e:nip")) != -1) {
        switch (c) {
            case 'h':
                usage();
                exit(retval);
                break;
            case 'a':
                scripty_data.sd_actual_name = optarg;
                break;
            case 'e':
                scripty_data.sd_expected_name = optarg;
                if ((file = fopen(optarg, "r")) == nullptr) {
                    fprintf(
                        stderr, "%s:error: cannot open %s\n", tstamp(), optarg);
                    retval = EXIT_FAILURE;
                } else {
                    char line[32 * 1024];

                    while (fgets(line, sizeof(line), file)) {
                        if (line[0] == 'K') {
                            struct command cmd;

                            cmd.c_type = CT_WRITE;
                            cmd.c_arg = hex2bits(&line[2]);
                            scripty_data.sd_replay.push_back(cmd);
                        }
                    }
                }
                break;
            case 'n':
                passout = false;
                break;
            case 'i':
                passin = true;
                break;
            case 'p':
                prompt = true;
                break;
            default:
                fprintf(stderr, "%s:error: unknown flag -- %c\n", tstamp(), c);
                retval = EXIT_FAILURE;
                break;
        }
    }

    argc -= optind;
    argv += optind;

    if (!scripty_data.sd_expected_name.empty()
        && scripty_data.sd_actual_name.empty())
    {
        scripty_data.sd_actual_name = scripty_data.sd_expected_name.filename();
        scripty_data.sd_actual_name += ".tmp";
    }

    if (!scripty_data.sd_actual_name.empty()) {
        if ((scripty_data.sd_from_child
             = fopen(scripty_data.sd_actual_name.c_str(), "w"))
            == nullptr)
        {
            fprintf(stderr,
                    "error: unable to open %s -- %s\n",
                    scripty_data.sd_actual_name.c_str(),
                    strerror(errno));
            retval = EXIT_FAILURE;
        }
    }

    if (scripty_data.sd_from_child != nullptr) {
        fcntl(fileno(scripty_data.sd_from_child), F_SETFD, 1);
    }

    if (retval != EXIT_FAILURE) {
        guard_termios gt(STDOUT_FILENO);
        fd = open("/tmp/scripty.err", O_WRONLY | O_CREAT | O_APPEND, 0666);
        dup2(fd, STDERR_FILENO);
        close(fd);
        fprintf(stderr, "%s:startup\n", tstamp());

        child_term ct(passin);

        if (ct.is_child()) {
            execvp(argv[0], argv);
            perror("execvp");
            exit(-1);
        } else {
            int maxfd;
            struct timeval last, now;
            fd_set read_fds;
            term_machine tm(ct);
            size_t last_replay_size = scripty_data.sd_replay.size();

            scripty_data.sd_child_pid = ct.get_child_pid();
            signal(SIGINT, sigpass);
            signal(SIGTERM, sigpass);

            signal(SIGCHLD, sigchld);

            gettimeofday(&now, nullptr);
            last = now;

            FD_ZERO(&read_fds);
            FD_SET(STDIN_FILENO, &read_fds);
            FD_SET(ct.get_fd(), &read_fds);

            fprintf(stderr, "%s:goin in the loop\n", tstamp());

            tty_raw(STDIN_FILENO);

            maxfd = max(STDIN_FILENO, ct.get_fd());
            while (scripty_data.sd_looping) {
                fd_set ready_rfds = read_fds;
                struct timeval diff, to;
                int rc;

                to.tv_sec = 0;
                to.tv_usec = 10000;
                rc = select(maxfd + 1, &ready_rfds, nullptr, nullptr, &to);
                gettimeofday(&now, nullptr);
                timersub(&now, &last, &diff);
                if (diff.tv_sec > 10) {
                    fprintf(stderr, "%s:replay timed out!\n", tstamp());
                    scripty_data.sd_looping = false;
                    kill(ct.get_child_pid(), SIGKILL);
                    retval = EXIT_FAILURE;
                    break;
                }
                if (rc == 0) {
                } else if (rc < 0) {
                    switch (errno) {
                        case EINTR:
                            break;
                        default:
                            fprintf(stderr,
                                    "%s:select %s\n",
                                    tstamp(),
                                    strerror(errno));
                            kill(ct.get_child_pid(), SIGKILL);
                            scripty_data.sd_looping = false;
                            break;
                    }
                } else {
                    char buffer[1024];

                    fprintf(stderr, "%s:fds ready %d\n", tstamp(), rc);
                    if (FD_ISSET(STDIN_FILENO, &ready_rfds)) {
                        rc = read(STDIN_FILENO, buffer, sizeof(buffer));
                        if (rc < 0) {
                            scripty_data.sd_looping = false;
                        } else if (rc == 0) {
                            FD_CLR(STDIN_FILENO, &read_fds);
                        } else {
                            log_perror(write(ct.get_fd(), buffer, rc));

                            for (ssize_t lpc = 0; lpc < rc; lpc++) {
                                fprintf(stderr,
                                        "%s:to-child %02x\n",
                                        tstamp(),
                                        buffer[lpc] & 0xff);
                                tm.new_user_input(buffer[lpc]);
                            }
                        }
                        last = now;
                    }
                    if (FD_ISSET(ct.get_fd(), &ready_rfds)) {
                        rc = read(ct.get_fd(), buffer, sizeof(buffer));
                        fprintf(stderr, "%s:read rc %d\n", tstamp(), rc);
                        if (rc <= 0) {
                            scripty_data.sd_looping = false;
                        } else {
                            if (passout) {
                                log_perror(write(STDOUT_FILENO, buffer, rc));
                            }
                            if (scripty_data.sd_from_child != nullptr) {
                                for (size_t lpc = 0; lpc < rc; lpc++) {
#if 0
                                    fprintf(stderr, "%s:from-child %02x\n",
                                            tstamp(),
                                            buffer[lpc] & 0xff);
#endif
                                    tm.new_input(buffer[lpc]);
                                    if (scripty_data.sd_replay.size()
                                        != last_replay_size)
                                    {
                                        last = now;
                                        last_replay_size
                                            = scripty_data.sd_replay.size();
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        retval = ct.wait_for_child() || retval;
    }

    if (retval == EXIT_SUCCESS && !scripty_data.sd_expected_name.empty()) {
        auto cmd = fmt::format("diff -ua {} {}",
                               scripty_data.sd_expected_name.string(),
                               scripty_data.sd_actual_name.string());
        auto rc = system(cmd.c_str());
        if (rc != 0) {
            if (prompt) {
                char resp[4];

                printf("Would you like to update the original file? (y/N) ");
                fflush(stdout);
                log_perror(scanf("%3s", resp));
                if (strcasecmp(resp, "y") == 0) {
                    printf("Updating: %s -> %s\n",
                           scripty_data.sd_actual_name.c_str(),
                           scripty_data.sd_expected_name.c_str());

                    auto options
                        = std::filesystem::copy_options::overwrite_existing;
                    std::filesystem::copy_file(scripty_data.sd_actual_name,
                                               scripty_data.sd_expected_name,
                                               options);
                } else {
                    retval = EXIT_FAILURE;
                }
            } else {
                fprintf(stderr, "%s:error: mismatch\n", tstamp());
                retval = EXIT_FAILURE;
            }
        }
    }

    return retval;
}
