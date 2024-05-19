/**
 * Copyright (c) 2023, Timothy Stack
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

#include <chrono>
#include <unordered_set>

#include "piper.looper.hh"

#include <arpa/inet.h>
#include <poll.h>

#include "ArenaAlloc/arenaalloc.h"
#include "base/date_time_scanner.hh"
#include "base/fs_util.hh"
#include "base/injector.hh"
#include "base/time_util.hh"
#include "config.h"
#include "hasher.hh"
#include "line_buffer.hh"
#include "lnav_config_fwd.hh"
#include "pcrepp/pcre2pp.hh"
#include "piper.looper.cfg.hh"
#include "robin_hood/robin_hood.h"

using namespace std::chrono_literals;

static ssize_t
write_line_meta(int fd, struct timeval& tv, log_level_t level, off_t woff)
{
    char time_str[64];
    auto fmt_res = fmt::format_to_n(time_str,
                                    sizeof(time_str),
                                    FMT_STRING("{: 12}.{:06}:{};"),
                                    tv.tv_sec,
                                    tv.tv_usec,
                                    level_names[level][0]);

    return pwrite(fd, time_str, fmt_res.size, woff);
}

extern char** environ;

namespace lnav {
namespace piper {

class piper_config_listener : public lnav_config_listener {
public:
    piper_config_listener() : lnav_config_listener(__FILE__) {}

    void reload_config(lnav_config_listener::error_reporter& reporter) override
    {
        static const auto KNOWN_CAPTURES
            = std::unordered_set<string_fragment,
                                 frag_hasher,
                                 std::equal_to<string_fragment>>{
                string_fragment::from_const("mux_id"),
                string_fragment::from_const("timestamp"),
                string_fragment::from_const("body"),
            };

        auto* cfg = injector::get<config*>();

        for (auto& demux_pair : cfg->c_demux_definitions) {
            auto pat = demux_pair.second.dd_pattern.pp_value;
            auto& dd = demux_pair.second;

            log_info("checking demux definition: %s", demux_pair.first.c_str());
            dd.dd_muxid_capture_index = pat->name_index("mux_id");

            if (dd.dd_muxid_capture_index < 0) {
                auto um = lnav::console::user_message::error(
                    "mux_id not found in pattern");

                reporter(&dd.dd_pattern, um);
                continue;
            }

            dd.dd_body_capture_index = pat->name_index("body");
            if (dd.dd_body_capture_index < 0) {
                auto um = lnav::console::user_message::error(
                    "body not found in pattern");

                reporter(&dd.dd_pattern, um);
                continue;
            }
            dd.dd_timestamp_capture_index = pat->name_index("timestamp");

            for (const auto& ncap : pat->get_named_captures()) {
                if (KNOWN_CAPTURES.count(ncap.get_name())) {
                    continue;
                }

                dd.dd_meta_capture_indexes[ncap.get_name().to_string()]
                    = ncap.get_index();
            }

            dd.dd_valid = true;
        }
    }
};

piper_config_listener _PIPER_LISTENER;

const json_path_container header_env_handlers = {
    yajlpp::pattern_property_handler("(?<name>.*)")
        .with_synopsis("<name>")
        .for_field(&lnav::piper::header::h_env),
};

const json_path_container header_demux_handlers = {
    yajlpp::pattern_property_handler("(?<name>.*)")
        .with_synopsis("<name>")
        .for_field(&lnav::piper::header::h_demux_meta),
};

static const json_path_handler_base::enum_value_t demux_output_values[] = {
    {"not_applicable", demux_output_t::not_applicable},
    {"signal", demux_output_t::signal},
    {"invalid", demux_output_t::invalid},

    json_path_handler_base::ENUM_TERMINATOR,
};

const typed_json_path_container<lnav::piper::header> header_handlers = {
    yajlpp::property_handler("name").for_field(&lnav::piper::header::h_name),
    yajlpp::property_handler("timezone")
        .for_field(&lnav::piper::header::h_timezone),
    yajlpp::property_handler("ctime").for_field(&lnav::piper::header::h_ctime),
    yajlpp::property_handler("cwd").for_field(&lnav::piper::header::h_cwd),
    yajlpp::property_handler("env").with_children(header_env_handlers),
    yajlpp::property_handler("mux_id").for_field(
        &lnav::piper::header::h_mux_id),
    yajlpp::property_handler("demux_output")
        .with_enum_values(demux_output_values)
        .for_field(&lnav::piper::header::h_demux_output),
    yajlpp::property_handler("demux_meta").with_children(header_demux_handlers),
};

static std::map<std::string, std::string>
environ_to_map()
{
    static const auto SENSITIVE_VARS
        = lnav::pcre2pp::code::from_const(R"((?i)token|pass)");

    std::map<std::string, std::string> retval;

    for (size_t lpc = 0; environ[lpc]; lpc++) {
        auto full_sf = string_fragment::from_c_str(environ[lpc]);
        auto pair_opt = full_sf.split_pair(string_fragment::tag1{'='});

        if (!pair_opt) {
            continue;
        }
        if (SENSITIVE_VARS.find_in(pair_opt->first).ignore_error()) {
            retval[pair_opt->first.to_string()] = "******";
        } else {
            retval[pair_opt->first.to_string()] = pair_opt->second.to_string();
        }
    }

    return retval;
}

looper::
looper(std::string name, auto_fd stdout_fd, auto_fd stderr_fd, options opts)
    : l_name(std::move(name)), l_cwd(ghc::filesystem::current_path().string()),
      l_env(environ_to_map()), l_stdout(std::move(stdout_fd)),
      l_stderr(std::move(stderr_fd)), l_options(opts)
{
    size_t count = 0;
    do {
        this->l_out_dir
            = storage_path()
            / fmt::format(
                  FMT_STRING("p-{}-{:03}"),
                  hasher().update(getmstime()).update(l_name).to_string(),
                  count);
        count += 1;
    } while (ghc::filesystem::exists(this->l_out_dir));
    ghc::filesystem::create_directories(this->l_out_dir);
    this->l_future = std::async(std::launch::async, [this]() { this->loop(); });
}

looper::~
looper()
{
    log_info("piper destructed, shutting down: %s", this->l_name.c_str());
    this->l_looping = false;
    this->l_future.wait();
}

enum class read_mode_t {
    binary,
    line,
};

void
looper::loop()
{
    static constexpr auto FORCE_MTIME_UPDATE_DURATION = 8h;
    static const auto DEFAULT_ID = string_fragment{};
    static const auto OUT_OF_FRAME_ID
        = string_fragment::from_const("_out_of_frame_");
    static constexpr auto FILE_TIMEOUT_BACKOFF = 30ms;
    static constexpr auto FILE_TIMEOUT_MAX = 1000ms;

    const auto& cfg = injector::get<const config&>();
    struct pollfd pfd[2];
    struct {
        line_buffer lb;
        file_range last_range;
        pollfd* pfd{nullptr};
        log_level_t cf_level{LEVEL_INFO};
        read_mode_t cf_read_mode{read_mode_t::line};

        void reset_pfd()
        {
            this->pfd->fd = this->lb.get_fd();
            this->pfd->events = POLLIN;
            this->pfd->revents = 0;
        }
    } captured_fds[2];
    struct out_state {
        auto_fd os_fd;
        off_t os_woff{0};
        off_t os_last_woff{0};
        std::string os_hash_id;
        std::optional<log_level_t> os_level;
    };
    robin_hood::unordered_map<string_fragment,
                              out_state,
                              frag_hasher,
                              std::equal_to<string_fragment>>
        outfds;
    size_t rotate_count = 0;
    std::optional<demux_def> curr_demux_def;
    auto md = lnav::pcre2pp::match_data::unitialized();
    ArenaAlloc::Alloc<char> sf_allocator{64 * 1024};
    bool demux_attempted = false;
    date_time_scanner dts;
    struct timeval line_tv;
    struct exttm line_tm;
    auto file_timeout = 0ms;
    multiplex_matcher mmatcher;

    log_info("starting loop to capture: %s (%d %d)",
             this->l_name.c_str(),
             this->l_stdout.get(),
             this->l_stderr.get());
    this->l_stdout.non_blocking();
    captured_fds[0].lb.set_fd(this->l_stdout);
    if (this->l_stderr.has_value()) {
        this->l_stderr.non_blocking();
        captured_fds[1].lb.set_fd(this->l_stderr);
    }
    captured_fds[1].cf_level = LEVEL_ERROR;
    auto last_write = std::chrono::system_clock::now();
    do {
        static constexpr auto TIMEOUT
            = std::chrono::duration_cast<std::chrono::milliseconds>(1s).count();

        auto poll_timeout = TIMEOUT;
        size_t used_pfds = 0;
        size_t file_count = 0;
        for (auto& cap : captured_fds) {
            cap.pfd = nullptr;
            if (cap.lb.get_fd() == -1) {
                continue;
            }

            if (!cap.lb.is_pipe()) {
                file_count += 1;
                poll_timeout = file_timeout.count();
            } else if (!cap.lb.is_pipe_closed()) {
                cap.pfd = &pfd[used_pfds];
                used_pfds += 1;
                cap.reset_pfd();
            }
        }

        if (used_pfds == 0 && file_count == 0) {
            log_info("inputs consumed, breaking loop: %s",
                     this->l_name.c_str());
            this->l_looping = false;
            break;
        }

        auto poll_rc = poll(pfd, used_pfds, poll_timeout);
        if (poll_rc == 0) {
            // update the timestamp to keep the file alive from any
            // cleanup processes
            for (const auto& outfd_pair : outfds) {
                auto now = std::chrono::system_clock::now();

                if ((now - last_write) >= FORCE_MTIME_UPDATE_DURATION) {
                    last_write = now;
                    log_perror(futimes(outfd_pair.second.os_fd.get(), nullptr));
                }
            }
            if (file_count == 0) {
                continue;
            }
        } else {
            last_write = std::chrono::system_clock::now();
        }
        for (auto& cap : captured_fds) {
            if (cap.lb.get_fd() == -1) {
                continue;
            }
            while (this->l_looping) {
                if (file_count == 0
                    && (cap.pfd == nullptr
                        || !(cap.pfd->revents & (POLLIN | POLLHUP))))
                {
                    break;
                }

                if (cap.cf_read_mode == read_mode_t::binary) {
                    char buffer[8192];
                    auto read_rc
                        = read(cap.lb.get_fd(), buffer, sizeof(buffer));

                    if (read_rc < 0) {
                        if (errno == EAGAIN) {
                            break;
                        }
                        log_error("failed to read next chunk: %s -- %s",
                                  this->l_name.c_str(),
                                  strerror(errno));
                        this->l_looping = false;
                    } else if (read_rc == 0) {
                        this->l_looping = false;
                    } else {
                        auto rc = write(
                            outfds[DEFAULT_ID].os_fd.get(), buffer, read_rc);
                        if (rc != read_rc) {
                            log_error(
                                "failed to write to capture file: %s -- %s",
                                this->l_name.c_str(),
                                strerror(errno));
                        }
                    }
                    continue;
                }

                auto load_result = cap.lb.load_next_line(cap.last_range);

                if (load_result.isErr()) {
                    log_error("failed to load next line: %s -- %s",
                              this->l_name.c_str(),
                              load_result.unwrapErr().c_str());
                    this->l_looping = false;
                    break;
                }

                auto li = load_result.unwrap();

                if (cap.last_range.fr_offset == 0 && !cap.lb.is_header_utf8()) {
                    log_info("switching capture to binary mode: %s",
                             this->l_name.c_str());
                    cap.cf_read_mode = read_mode_t::binary;

                    auto out_path = this->l_out_dir / "out.0";
                    log_info("creating binary capture file: %s -- %s",
                             this->l_name.c_str(),
                             out_path.c_str());
                    auto create_res = lnav::filesystem::create_file(
                        out_path, O_WRONLY | O_CLOEXEC | O_TRUNC, 0600);
                    if (create_res.isErr()) {
                        log_error("unable to open capture file: %s -- %s",
                                  this->l_name.c_str(),
                                  create_res.unwrapErr().c_str());
                        break;
                    }

                    auto hdr_path = this->l_out_dir / ".header";
                    auto hdr = header{
                        current_timeval(),
                        this->l_name,
                        this->l_cwd,
                        this->l_env,
                    };
                    auto write_hdr_res = lnav::filesystem::write_file(
                        hdr_path, header_handlers.to_string(hdr));
                    if (write_hdr_res.isErr()) {
                        log_error("unable to write header file: %s -- %s",
                                  hdr_path.c_str(),
                                  write_hdr_res.unwrapErr().c_str());
                        break;
                    }

                    outfds[DEFAULT_ID].os_fd = create_res.unwrap();
                    auto header_avail = cap.lb.get_available();
                    auto read_res = cap.lb.read_range(header_avail);
                    if (read_res.isOk()) {
                        auto sbr = read_res.unwrap();
                        write(outfds[DEFAULT_ID].os_fd.get(),
                              sbr.get_data(),
                              sbr.length());
                    } else {
                        log_error("failed to get header data: %s -- %s",
                                  this->l_name.c_str(),
                                  read_res.unwrapErr().c_str());
                    }
                    continue;
                }

                if (li.li_file_range.empty()) {
                    if (!this->l_options.o_tail) {
                        log_info("%s: reached EOF, exiting",
                                 this->l_name.c_str());
                        this->l_looping = false;
                    }
                    if (file_count > 0 && file_timeout < FILE_TIMEOUT_MAX) {
                        file_timeout += FILE_TIMEOUT_BACKOFF;
                    }
                    break;
                }
                if (file_count > 0) {
                    file_timeout = 0ms;
                }

                if (li.li_partial && !cap.lb.is_pipe_closed()) {
                    break;
                }

                auto read_result = cap.lb.read_range(li.li_file_range);
                if (read_result.isErr()) {
                    log_error("failed to read next line: %s -- %s",
                              this->l_name.c_str(),
                              read_result.unwrapErr().c_str());
                    this->l_looping = false;
                    break;
                }

                auto demux_output = demux_output_t::not_applicable;
                auto sbr = read_result.unwrap();
                auto line_muxid_sf = DEFAULT_ID;
                auto body_sf = sbr.to_string_fragment();
                auto ts_sf = string_fragment{};
                if (!curr_demux_def && !demux_attempted) {
                    log_trace("demux input line: %s",
                              fmt::format(FMT_STRING("{:?}"), body_sf).c_str());

                    auto match_res = mmatcher.match(body_sf);
                    demux_attempted = match_res.match(
                        [this, &curr_demux_def, &cfg](
                            multiplex_matcher::found f) {
                            curr_demux_def
                                = cfg.c_demux_definitions.find(f.f_id)->second;
                            {
                                safe::WriteAccess<safe_demux_id> di(
                                    this->l_demux_id);

                                di->assign(f.f_id);
                            }
                            return true;
                        },
                        [](multiplex_matcher::not_found nf) { return true; },
                        [](multiplex_matcher::partial p) { return false; });
                    if (!demux_attempted) {
                        cap.last_range = li.li_file_range;
                        continue;
                    }
                }
                std::optional<log_level_t> demux_level;
                if (curr_demux_def
                    && curr_demux_def->dd_pattern.pp_value
                           ->capture_from(body_sf)
                           .into(md)
                           .matches()
                           .ignore_error())
                {
                    auto muxid_cap_opt
                        = md[curr_demux_def->dd_muxid_capture_index];
                    auto body_cap_opt
                        = md[curr_demux_def->dd_body_capture_index];
                    if (muxid_cap_opt && body_cap_opt) {
                        line_muxid_sf = muxid_cap_opt.value();
                        body_sf = body_cap_opt.value();
                        demux_output = demux_output_t::signal;
                    } else {
                        demux_output = demux_output_t::invalid;
                        line_muxid_sf = OUT_OF_FRAME_ID;
                        demux_level = LEVEL_ERROR;
                    }
                    if (curr_demux_def->dd_timestamp_capture_index >= 0) {
                        auto ts_cap_opt
                            = md[curr_demux_def->dd_timestamp_capture_index];
                        if (ts_cap_opt) {
                            ts_sf = ts_cap_opt.value();
                        }
                    }
                } else if (curr_demux_def) {
                    if (curr_demux_def->dd_control_pattern.pp_value
                        && curr_demux_def->dd_control_pattern.pp_value
                               ->find_in(body_sf)
                               .ignore_error())
                    {
                        cap.last_range = li.li_file_range;
                        continue;
                    }

                    demux_output = demux_output_t::invalid;
                    line_muxid_sf = OUT_OF_FRAME_ID;
                    demux_level = LEVEL_ERROR;
                }

                auto outfds_iter = outfds.find(line_muxid_sf);
                if (outfds_iter == outfds.end()) {
                    line_muxid_sf = line_muxid_sf.to_owned(sf_allocator);
                    auto emp_res
                        = outfds.emplace(line_muxid_sf, out_state{auto_fd{-1}});
                    outfds_iter = emp_res.first;
                    outfds_iter->second.os_hash_id
                        = hasher().update(line_muxid_sf).to_string();
                    outfds_iter->second.os_level = demux_level;
                }
                auto& os = outfds_iter->second;
                if (os.os_woff > os.os_last_woff
                    && os.os_woff >= cfg.c_max_size)
                {
                    log_info(
                        "capture file has reached max size, rotating: %s -- "
                        "%lld",
                        this->l_name.c_str(),
                        os.os_woff);
                    os.os_fd.reset();
                }

                if (!os.os_fd.has_value()) {
                    auto tmp_path = this->l_out_dir
                        / fmt::format(FMT_STRING("tmp.{}.{}"),
                                      os.os_hash_id,
                                      rotate_count % cfg.c_rotations);
                    log_info("creating capturing file: %s (mux_id: %.*s) -- %s",
                             this->l_name.c_str(),
                             line_muxid_sf.length(),
                             line_muxid_sf.data(),
                             tmp_path.c_str());
                    auto create_res = lnav::filesystem::create_file(
                        tmp_path, O_WRONLY | O_CLOEXEC | O_TRUNC, 0600);
                    if (create_res.isErr()) {
                        log_error("unable to open capture file: %s -- %s",
                                  this->l_name.c_str(),
                                  create_res.unwrapErr().c_str());
                        break;
                    }

                    os.os_fd = create_res.unwrap();
                    rotate_count += 1;

                    auto hdr = header{
                        current_timeval(),
                        this->l_name,
                        this->l_cwd,
                        this->l_env,
                        "",
                        line_muxid_sf.to_string(),
                        demux_output,
                    };
                    hdr.h_demux_output = demux_output;
                    if (!line_muxid_sf.empty()) {
                        hdr.h_name = fmt::format(
                            FMT_STRING("{}/{}"), hdr.h_name, line_muxid_sf);
                        hdr.h_timezone = "UTC";

                        for (const auto& meta_cap :
                             curr_demux_def->dd_meta_capture_indexes)
                        {
                            auto mc_opt = md[meta_cap.second];
                            if (!mc_opt) {
                                continue;
                            }

                            hdr.h_demux_meta[meta_cap.first]
                                = mc_opt.value().to_string();
                        }
                    }

                    os.os_woff = 0;
                    auto hdr_str = header_handlers.to_string(hdr);
                    uint32_t meta_size = htonl(hdr_str.length());
                    auto prc = write(
                        os.os_fd.get(), HEADER_MAGIC, sizeof(HEADER_MAGIC));
                    if (prc < sizeof(HEADER_MAGIC)) {
                        log_error("unable to write file header: %s -- %s",
                                  this->l_name.c_str(),
                                  strerror(errno));
                        break;
                    }
                    os.os_woff += prc;
                    prc = write(os.os_fd.get(), &meta_size, sizeof(meta_size));
                    if (prc < sizeof(meta_size)) {
                        log_error("unable to write file header: %s -- %s",
                                  this->l_name.c_str(),
                                  strerror(errno));
                        break;
                    }
                    os.os_woff += prc;
                    prc = write(
                        os.os_fd.get(), hdr_str.c_str(), hdr_str.size());
                    if (prc < hdr_str.size()) {
                        log_error("unable to write file header: %s -- %s",
                                  this->l_name.c_str(),
                                  strerror(errno));
                        break;
                    }
                    os.os_woff += prc;

                    auto out_path = this->l_out_dir
                        / fmt::format(FMT_STRING("out.{}.{}"),
                                      os.os_hash_id,
                                      rotate_count % cfg.c_rotations);
                    ghc::filesystem::rename(tmp_path, out_path);
                }

                ssize_t wrc;

                os.os_last_woff = os.os_woff;
                if (!ts_sf.empty()
                    && dts.scan(ts_sf.data(),
                                ts_sf.length(),
                                nullptr,
                                &line_tm,
                                line_tv,
                                false))
                {
                } else {
                    gettimeofday(&line_tv, nullptr);
                }
                wrc = write_line_meta(os.os_fd.get(),
                                      line_tv,
                                      os.os_level.value_or(cap.cf_level),
                                      os.os_woff);
                if (wrc == -1) {
                    log_error("unable to write timestamp: %s -- %s",
                              this->l_name.c_str(),
                              strerror(errno));
                    this->l_looping = false;
                    break;
                }
                os.os_woff += wrc;

                /* Need to do pwrite here since the fd is used by the main
                 * lnav process as well.
                 */
                wrc = pwrite(os.os_fd.get(),
                             body_sf.data(),
                             body_sf.length(),
                             os.os_woff);
                if (wrc == -1) {
                    log_error("unable to write captured data: %s -- %s",
                              this->l_name.c_str(),
                              strerror(errno));
                    this->l_looping = false;
                    break;
                }
                os.os_woff += wrc;
                if (!body_sf.endswith("\n")) {
                    wrc = pwrite(os.os_fd.get(), "\n", 1, os.os_woff);
                    if (wrc == -1) {
                        log_error("unable to write captured data: %s -- %s",
                                  this->l_name.c_str(),
                                  strerror(errno));
                        this->l_looping = false;
                        break;
                    }
                    os.os_woff += wrc;
                }

                cap.last_range = li.li_file_range;
                if (li.li_partial && sbr.get_data()[sbr.length() - 1] != '\n'
                    && (cap.last_range.next_offset() != cap.lb.get_file_size()))
                {
                    os.os_woff = os.os_last_woff;
                }
            }
        }
        this->l_loop_count += 1;
    } while (this->l_looping);

    log_info("exiting loop to capture: %s", this->l_name.c_str());
}

Result<handle<state::running>, std::string>
create_looper(std::string name,
              auto_fd stdout_fd,
              auto_fd stderr_fd,
              options opts)
{
    return Ok(handle<state::running>(std::make_shared<looper>(
        name, std::move(stdout_fd), std::move(stderr_fd), opts)));
}

void
cleanup()
{
    (void) std::async(std::launch::async, []() {
        const auto& cfg = injector::get<const config&>();
        auto now = std::chrono::system_clock::now();
        auto cache_path = storage_path();
        std::vector<ghc::filesystem::path> to_remove;

        for (const auto& cache_subdir :
             ghc::filesystem::directory_iterator(cache_path))
        {
            auto mtime = ghc::filesystem::last_write_time(cache_subdir.path());
            auto exp_time = mtime + cfg.c_ttl;
            if (now < exp_time) {
                continue;
            }

            bool is_recent = false;

            for (const auto& entry :
                 ghc::filesystem::directory_iterator(cache_subdir))
            {
                auto mtime = ghc::filesystem::last_write_time(entry.path());
                auto exp_time = mtime + cfg.c_ttl;
                if (now < exp_time) {
                    is_recent = true;
                    break;
                }
            }
            if (!is_recent) {
                to_remove.emplace_back(cache_subdir);
            }
        }

        for (auto& entry : to_remove) {
            log_debug("removing piper directory: %s", entry.c_str());
            ghc::filesystem::remove_all(entry);
        }
    });
}

}  // namespace piper
}  // namespace lnav
