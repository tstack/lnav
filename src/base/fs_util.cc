/**
 * Copyright (c) 2022, Timothy Stack
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

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include "fs_util.hh"

#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <glob.h>
#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/param.h>
#include <unistd.h>

#ifdef HAVE_SYS_SYSCTL_H
#    include <sys/sysctl.h>
#endif

#include "config.h"
#include "fmt/format.h"
#include "itertools.hh"
#include "lnav_log.hh"
#include "opt_util.hh"
#include "pcrepp/pcre2pp.hh"
#include "scn/scan.h"
#include "short_alloc.h"
#include "string_util.hh"

#ifdef HAVE_LIBPROC_H
#    include <libproc.h>
#endif

namespace lnav::filesystem {
static bool
have_cygdrive()
{
    static const auto RETVAL = access("/cygdrive", X_OK) == 0;

    return RETVAL;
}

std::string
escape_glob_for_win(std::string arg)
{
#if defined(__MSYS__)
    std::replace(arg.begin(), arg.end(), '\\', '/');
    std::replace(arg.begin(), arg.end(), '^', '\\');
    return arg;
#else
    return arg;
#endif
}

std::string
expand_tilde(const std::string& path)
{
    if (path.empty() || path[0] != '~') {
        return path;
    }

    auto slash_pos = path.find('/');
    auto user_end = slash_pos == std::string::npos ? path.size() : slash_pos;
    std::optional<std::string> home;

    if (user_end == 1) {
        const auto* home_env = getenv("HOME");
        if (home_env != nullptr) {
            home = home_env;
        } else {
            const auto* pw = getpwuid(getuid());
            if (pw != nullptr) {
                home = pw->pw_dir;
            }
        }
    } else {
        auto username = path.substr(1, user_end - 1);
        const auto* pw = getpwnam(username.c_str());
        if (pw != nullptr) {
            home = pw->pw_dir;
        }
    }

    if (!home) {
        return path;
    }

    return home.value() + path.substr(user_end);
}

bool
is_recursive_glob(const std::string& fn)
{
    size_t start = 0;

    while (start <= fn.size()) {
        auto end = fn.find('/', start);
        if (end == std::string::npos) {
            end = fn.size();
        }
        if (end - start == 2 && fn.compare(start, 2, "**") == 0) {
            return true;
        }
        start = end + 1;
    }

    return false;
}

bool
matches_file_pattern(const std::string& pattern, const std::string& path)
{
    if (is_recursive_glob(pattern)) {
        return glob_match(pattern, path);
    }

    return fnmatch(pattern.c_str(), path.c_str(), 0) == 0;
}

std::pair<std::string, std::string>
split_glob_prefix(const std::string& pattern)
{
    size_t prefix_end = 0;
    size_t start = 0;

    while (true) {
        auto end = pattern.find('/', start);
        if (end == std::string::npos) {
            break;
        }
        if (is_glob(pattern.substr(start, end - start))) {
            break;
        }
        prefix_end = end + 1;
        start = end + 1;
    }

    return {pattern.substr(0, prefix_end), pattern.substr(prefix_end)};
}

recursive_glob::recursive_glob(std::string pattern)
    : rg_pattern(std::move(pattern))
{
    size_t start = 0;
    auto found_globstar = false;

    while (start <= this->rg_pattern.size()) {
        auto end = this->rg_pattern.find('/', start);
        if (end == std::string::npos) {
            end = this->rg_pattern.size();
        }

        auto comp_len = end - start;
        if (found_globstar) {
            if (comp_len > 0 && this->rg_pattern[start] == '.') {
                this->rg_allow_hidden = true;
            }
        } else if (comp_len == 2
                   && this->rg_pattern.compare(start, 2, "**") == 0)
        {
            found_globstar = true;
            this->rg_prefix = this->rg_pattern.substr(0, start);
            if (this->rg_prefix.size() > 1 && this->rg_prefix.back() == '/') {
                this->rg_prefix.pop_back();
            }
        }
        start = end + 1;
    }
}

std::vector<std::string>
recursive_glob::find_roots() const
{
    if (!is_glob(this->rg_prefix)) {
        return {this->rg_prefix};
    }

    std::vector<std::string> retval;
    glob_t gl;

    memset(&gl, 0, sizeof(gl));
    auto win_prefix = escape_glob_for_win(this->rg_prefix);
    if (glob(win_prefix.c_str(), GLOB_MARK, nullptr, &gl) == 0) {
        for (size_t lpc = 0; lpc < gl.gl_pathc; lpc++) {
            std::string path = gl.gl_pathv[lpc];

            // GLOB_MARK adds a slash to the directories
            if (path.size() > 1 && path.back() == '/') {
                path.pop_back();
                retval.emplace_back(std::move(path));
            }
        }
    }
    globfree(&gl);

    return retval;
}

const recursive_glob::dir_state*
recursive_glob::lookup(const std::string& dir, size_t pass)
{
    // An empty directory is the current directory, which is kept out of
    // the paths so they come out spelled like the pattern.
    auto dir_path = std::filesystem::path(dir.empty() ? "." : dir);
    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(dir_path, ec);
    if (ec) {
        return nullptr;
    }

    auto& ds = this->rg_dirs[dir];
    auto cached = ds.ds_pass != 0 && ds.ds_mtime == mtime;
    ds.ds_pass = pass;
    if (cached) {
        return &ds;
    }

    ds.ds_mtime = mtime;
    ds.ds_subdirs.clear();
    ds.ds_files.clear();

    auto dir_iter = std::filesystem::directory_iterator(
        dir_path, std::filesystem::directory_options::skip_permission_denied, ec);
    for (; !ec && dir_iter != std::filesystem::directory_iterator();
         dir_iter.increment(ec))
    {
        const auto& entry = *dir_iter;
        auto name = entry.path().filename().string();

        if (!this->rg_allow_hidden && startswith(name, ".")) {
            continue;
        }

        std::string child;
        if (dir.empty()) {
            child = name;
        } else if (dir == "/") {
            child = "/" + name;
        } else {
            child = dir + "/" + name;
        }

        std::error_code entry_ec;
        if (entry.is_symlink(entry_ec)) {
            // Symbolic links to directories are not followed to avoid
            // loops, but links to files are fine.
            if (!entry.is_regular_file(entry_ec)) {
                continue;
            }
        } else if (entry.is_directory(entry_ec)) {
            ds.ds_subdirs.emplace_back(std::move(child));
            continue;
        } else if (!entry.is_regular_file(entry_ec)) {
            continue;
        }

        if (glob_match(this->rg_pattern, child)) {
            ds.ds_files.emplace_back(std::move(child));
        }
    }
    if (ec) {
        log_warning("unable to read directory for glob: %s -- %s",
                    dir_path.c_str(),
                    ec.message().c_str());
    }

    std::sort(ds.ds_subdirs.begin(), ds.ds_subdirs.end());
    std::sort(ds.ds_files.begin(), ds.ds_files.end());

    return &ds;
}

void
recursive_glob::end_pass(size_t pass)
{
    if (pass != this->rg_pass) {
        return;
    }

    for (auto iter = this->rg_dirs.begin(); iter != this->rg_dirs.end();) {
        if (iter->second.ds_pass == pass) {
            ++iter;
        } else {
            iter = this->rg_dirs.erase(iter);
        }
    }
}

recursive_glob::iterator
recursive_glob::begin(size_t max_dirs)
{
    require(max_dirs > 0);

    iterator retval;

    this->rg_pass += 1;
    retval.i_parent = this;
    retval.i_pass = this->rg_pass;
    retval.i_dirs = this->find_roots();
    std::sort(retval.i_dirs.rbegin(), retval.i_dirs.rend());
    retval.i_dir_budget = max_dirs;
    retval.i_state = iterator::state_t::paused;
    retval.advance();

    return retval;
}

std::optional<std::string>
recursive_glob::iterator::next_top_dir() const
{
    if (this->i_dirs.empty()) {
        return std::nullopt;
    }

    // Each component of the prefix, glob or not, matches exactly one
    // component of a root.
    const auto& prefix = this->i_parent->rg_prefix;
    size_t prefix_comps = 0;
    for (size_t lpc = 0; lpc < prefix.size(); lpc++) {
        if (prefix[lpc] != '/' && (lpc == 0 || prefix[lpc - 1] == '/')) {
            prefix_comps += 1;
        }
    }

    const auto& dir = this->i_dirs.back();
    size_t pos = startswith(dir, "/") ? 1 : 0;
    for (size_t lpc = 0; lpc <= prefix_comps; lpc++) {
        pos = dir.find('/', pos);
        if (pos == std::string::npos) {
            return dir;
        }
        pos += 1;
    }

    return dir.substr(0, pos - 1);
}

recursive_glob::iterator&
recursive_glob::iterator::operator++()
{
    if (this->at_match()) {
        this->i_file_index += 1;
        this->advance();
    }

    return *this;
}

void
recursive_glob::iterator::resume(size_t max_dirs)
{
    require(max_dirs > 0);

    this->i_dir_budget = max_dirs;
    if (this->i_state == state_t::paused) {
        this->advance();
    }
}

void
recursive_glob::iterator::advance()
{
    while (this->i_file_index >= this->i_files.size()) {
        this->i_files.clear();
        this->i_file_index = 0;

        if (this->i_dirs.empty()) {
            this->i_state = state_t::finished;
            this->i_parent->end_pass(this->i_pass);
            return;
        }
        if (this->i_dir_budget == 0) {
            this->i_state = state_t::paused;
            return;
        }
        this->i_dir_budget -= 1;
        this->i_dirs_visited += 1;

        auto dir = std::move(this->i_dirs.back());
        this->i_dirs.pop_back();

        const auto* ds = this->i_parent->lookup(dir, this->i_pass);
        if (ds == nullptr) {
            continue;
        }

        this->i_files = ds->ds_files;
        this->i_dirs.insert(
            this->i_dirs.end(), ds->ds_subdirs.rbegin(), ds->ds_subdirs.rend());
    }

    this->i_state = state_t::match;
    this->i_match_count += 1;
}

std::optional<std::filesystem::path>
self_path()
{
#if defined(HAVE_LIBPROC_H) && defined(PROC_PIDPATHINFO_MAXSIZE)
    auto pid = getpid();
    char pathbuf[PROC_PIDPATHINFO_MAXSIZE];

    auto rc = proc_pidpath(pid, pathbuf, sizeof(pathbuf));
    if (rc <= 0) {
        log_error("unable to determine self path: %s",
                  lnav::from_errno().message().c_str());
    } else {
        log_info("self path: %s", pathbuf);
        return std::filesystem::path(pathbuf);
    }
    return std::nullopt;
#elif defined(HAVE_SYS_SYSCTL_H) && defined(KERN_PROC_PATHNAME)
    char path[1024];
    int mib[4];
    size_t len = sizeof(path);

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PATHNAME;
    mib[3] = -1;  // current process

    if (sysctl(mib, 4, path, &len, NULL, 0) == 0) {
        return std::filesystem::path(path);
    }
    log_error("unable to determine path: %s", strerror(errno));
    return std::nullopt;
#else
    std::error_code ec;
    auto target = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        log_error("failed to read /proc/self/exe: %s", ec.message().c_str());
        return std::nullopt;
    }
    return target;
#endif
}

static time64_t
init_self_mtime()
{
    auto retval = time_t{};
    auto path_opt = self_path();

    time(&retval);
    if (path_opt) {
        auto stat_res = stat_file(path_opt.value());
        if (stat_res.isErr()) {
            log_error("unable to stat self: %s", stat_res.unwrapErr().c_str());
        } else {
            retval = stat_res.unwrap().st_mtime;
        }
    }

    return retval;
}

time64_t
self_mtime()
{
    static auto RETVAL = init_self_mtime();

    return RETVAL;
}

std::string
escape_path(const std::filesystem::path& p, path_type pt)
{
    auto p_str = p.string();
    std::string retval;

    for (const auto ch : p_str) {
        switch (ch) {
            case ' ':
            case '$':
            case '\\':
            case ';':
            case '&':
            case '<':
            case '>':
            case '\'':
            case '"':
                retval.push_back('\\');
                break;
            case '*':
            case '[':
            case ']':
            case '?':
                switch (pt) {
                    case path_type::normal:
                    case path_type::windows:
                    case path_type::remote:
                    case path_type::url:
                        retval.push_back('\\');
                        break;
                    case path_type::pattern:
                        break;
                }
                break;
            default:
                break;
        }
        retval.push_back(ch);
    }

    return retval;
}

bool
contains_dotdot(const std::filesystem::path& p)
{
    for (const auto& part : p) {
        if (part == "..") {
            return true;
        }
    }

    return false;
}

bool
is_url(const std::string& fn)
{
    static const auto url_re
        = lnav::pcre2pp::code::from_const("^(file|https?|ftps?|scp|sftp):.*");

    return url_re.find_in(fn).ignore_error().has_value();
}

path_type
determine_path_type(const std::string& arg)
{
    if (is_glob(arg)) {
        return path_type::pattern;
    }

    if (is_url(arg)) {
        return path_type::url;
    }

    const auto colon_pos = arg.find(':');
    if (colon_pos == std::string::npos) {
        return path_type::normal;
    }
    if (colon_pos == 1) {
        return path_type::windows;
    }
    return path_type::remote;
}

path_transcoder
path_transcoder::from(std::string arg)
{
    if (cget(arg, 1).value_or('\0') != ':') {
        std::optional<bool> caps;
#if defined(__MSYS__)
        if (arg.find('\\') != std::string::npos) {
            std::replace(arg.begin(), arg.end(), '\\', '/');
            caps = false;
        }
        if (startswith(arg, "//")) {
        } else if (startswith(arg, "/")) {
            auto cwd = std::filesystem::current_path();
            auto cwd_iter = std::next(cwd.begin());
            auto cwd_first_str = cwd_iter->string();
            if (cwd_first_str == "cygdrive") {
                auto drive_iter = std::next(cwd_iter);
                if (drive_iter != cwd.end()) {
                    auto cwd_drive_str = drive_iter->string();
                    arg.insert(0, cwd_drive_str);
                    arg.insert(0, "/");
                    caps = true;
                }
            }
            arg.insert(0, cwd_first_str);
            arg.insert(0, "/");
        }
#endif
        return {arg, caps};
    }

    bool caps = isupper(arg[0]);
    if (caps) {
        arg[0] = tolower(arg[0]);
    }

    switch (cget(arg, 2).value_or('\0')) {
        case '\\':
        case '/':
            arg.erase(1, 1);
            break;
        default:
            arg[1] = '/';
            break;
    }

    arg.insert(arg.begin(), '/');
    if (have_cygdrive()) {
        arg.insert(0, "/cygdrive");
    }
    std::replace(arg.begin(), arg.end(), '\\', '/');

    return {arg, caps};
}

std::string
path_transcoder::to_native(std::string arg)
{
    if (arg.empty() || !this->pt_root_name_capitalized) {
        return arg;
    }

    static const auto CYGDRIVE = "/cygdrive"_frag;

    if (startswith(arg, CYGDRIVE.data())) {
        arg.erase(0, CYGDRIVE.length());
    }

    if (arg[0] == '/' && !startswith(arg, "//")) {
        arg.erase(0, 1);
        if (cget(arg, 1).value_or('\0') == '/') {
            arg.insert(1, ":");
        }
    }
    if (this->pt_root_name_capitalized.value()) {
        arg[0] = toupper(arg[0]);
    }
    std::replace(arg.begin(), arg.end(), '/', '\\');

    return arg;
}

std::string
path_transcoder::to_shell_arg(std::string arg)
{
    static const auto plain_path_re
        = lnav::pcre2pp::code::from_const(R"(^[\w/]+$)");

    if (plain_path_re.find_in(arg).ignore_error()) {
        return arg;
    }

    // XXX
    return fmt::format(FMT_STRING("'{}'"), arg);
}

std::pair<std::string, file_location_t>
split_file_location(const std::string& file_path_str)
{
    if (access(file_path_str.c_str(), R_OK) == 0) {
        return {file_path_str, file_location_t{default_for_text_format{}}};
    }

    auto colon_index = file_path_str.rfind(':');
    if (colon_index != std::string::npos) {
        auto top_range
            = std::string_view{&file_path_str[colon_index + 1],
                               file_path_str.size() - colon_index - 1};
        auto scan_res = scn::scan_value<int>(top_range);

        if (scan_res && scan_res->range().empty()) {
            return std::make_pair(file_path_str.substr(0, colon_index),
                                  scan_res->value());
        }
        log_info("did not parse line number from file path with colon: %s",
                 file_path_str.c_str());
    }

    auto hash_index = file_path_str.rfind('#');
    if (hash_index != std::string::npos) {
        return std::make_pair(file_path_str.substr(0, hash_index),
                              file_path_str.substr(hash_index));
    }

    return std::make_pair(file_path_str,
                          file_location_t{default_for_text_format{}});
}

Result<std::filesystem::path, std::string>
realpath(const std::filesystem::path& path)
{
    char resolved[PATH_MAX];
    auto rc = ::realpath(path.c_str(), resolved);

    if (rc == nullptr) {
        return Err(lnav::from_errno().message());
    }

    return Ok(std::filesystem::path(resolved));
}

Result<auto_fd, std::string>
create_file(const std::filesystem::path& path, int flags, mode_t mode)
{
    auto fd = openp(path, flags | O_CREAT, mode);

    if (fd == -1) {
        return Err(fmt::format(FMT_STRING("Failed to open: {} -- {}"),
                               path.string(),
                               lnav::from_errno()));
    }

    return Ok(auto_fd(fd));
}

Result<auto_fd, std::string>
open_file(const std::filesystem::path& path, int flags)
{
    auto fd = openp(path, flags);

    if (fd == -1) {
        return Err(fmt::format(FMT_STRING("Failed to open: {} -- {}"),
                               path.string(),
                               lnav::from_errno()));
    }

    return Ok(auto_fd(fd));
}

Result<std::pair<std::filesystem::path, auto_fd>, std::string>
open_temp_file(const std::filesystem::path& pattern)
{
    auto pattern_str = pattern.string();
    stack_buf allocator;
    auto* pattern_copy = allocator.allocate(pattern_str.size() + 1);
    int fd;

    strcpy(pattern_copy, pattern_str.c_str());
#if HAVE_MKOSTEMP
    fd = mkostemp(pattern_copy, O_CLOEXEC);
#else
    fd = mkstemp(pattern_copy);
    if (fd != -1) {
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
#endif
    if (fd == -1) {
        return Err(
            fmt::format(FMT_STRING("unable to create temporary file: {} -- {}"),
                        pattern.string(),
                        lnav::from_errno()));
    }

    return Ok(std::make_pair(std::filesystem::path(pattern_copy), auto_fd(fd)));
}

Result<std::string, std::string>
read_file(const std::filesystem::path& path)
{
    try {
        std::ifstream file_stream(path);

        if (!file_stream) {
            return Err(lnav::from_errno().message());
        }

        std::string retval;
        retval.assign((std::istreambuf_iterator<char>(file_stream)),
                      std::istreambuf_iterator<char>());
        return Ok(retval);
    } catch (const std::exception& e) {
        return Err(std::string(e.what()));
    }
}

Result<write_file_result, std::string>
write_file(const std::filesystem::path& path,
           string_fragment_producer& content,
           lnav::enums::bitset<write_file_options> options)
{
    write_file_result retval;
    auto tmp_pattern = path;
    tmp_pattern += ".XXXXXX";

    auto tmp_pair = TRY(open_temp_file(tmp_pattern));
    auto for_res = content.for_each(
        [&tmp_pair](string_fragment sf) -> Result<void, std::string> {
            while (!sf.empty()) {
                auto bytes_written
                    = write(tmp_pair.second.get(), sf.data(), sf.length());
                if (bytes_written < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    return Err(fmt::format(
                        FMT_STRING(
                            "unable to write to temporary file {}: {}"),
                        tmp_pair.first.string(),
                        lnav::from_errno()));
                }

                sf = sf.substr(bytes_written);
            }

            return Ok();
        });

    if (for_res.isErr()) {
        return Err(for_res.unwrapErr());
    }

    std::error_code ec;
    if (options.is_set<write_file_options::backup_existing>()) {
        if (std::filesystem::exists(path, ec)) {
            auto backup_path = path;

            backup_path += ".bak";
            std::filesystem::rename(path, backup_path, ec);
            if (ec) {
                return Err(
                    fmt::format(FMT_STRING("unable to backup file {}: {}"),
                                path.string(),
                                ec.message()));
            }

            retval.wfr_backup_path = backup_path;
        }
    }

    auto mode = S_IRUSR | S_IWUSR;
    if (options.is_set<write_file_options::executable>()) {
        mode |= S_IXUSR;
    }
    if (options.is_set<write_file_options::read_only>()) {
        mode &= ~S_IWUSR;
    }

    fchmod(tmp_pair.second.get(), mode);

    std::filesystem::rename(tmp_pair.first, path, ec);
    if (ec) {
        return Err(
            fmt::format(FMT_STRING("unable to move temporary file {}: {}"),
                        tmp_pair.first.string(),
                        ec.message()));
    }

    log_debug("wrote file: %s", path.c_str());
    return Ok(retval);
}

std::string
build_path(const std::vector<std::filesystem::path>& paths)
{
    return paths
        | lnav::itertools::map([](const auto& path) { return path.string(); })
        | lnav::itertools::append(getenv_opt("PATH").value_or(""))
        | lnav::itertools::filter_out(&std::string::empty)
        | lnav::itertools::fold(
               [](const auto& elem, auto& accum) {
                   if (!accum.empty()) {
                       accum.push_back(':');
                   }
                   return accum.append(elem);
               },
               std::string());
}

Result<struct stat, std::string>
stat_file(const std::filesystem::path& path)
{
    struct stat retval;

    if (statp(path, &retval) == 0) {
        return Ok(retval);
    }

    return Err(fmt::format(FMT_STRING("failed to find file: {} -- {}"),
                           path.string(),
                           lnav::from_errno()));
}

file_lock::file_lock(const std::filesystem::path& archive_path)
{
    auto lock_path = archive_path;

    lock_path += ".lck";
    auto open_res
        = lnav::filesystem::create_file(lock_path, O_RDWR | O_CLOEXEC, 0600);
    if (open_res.isErr()) {
        throw std::runtime_error(open_res.unwrapErr());
    }
    this->lh_fd = open_res.unwrap();
}
}  // namespace lnav::filesystem

namespace fmt {
auto
formatter<std::filesystem::path>::format(const std::filesystem::path& p,
                                         format_context& ctx)
    -> decltype(ctx.out()) const
{
    auto esc_res = fmt::v10::detail::find_escape(
        p.native().data(), p.native().data() + p.native().size());
    if (esc_res.end == nullptr) {
        return formatter<string_view>::format(p.native(), ctx);
    }

    return format_to(ctx.out(), FMT_STRING("{:?}"), p.native());
}
}  // namespace fmt