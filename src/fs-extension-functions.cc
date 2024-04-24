/**
 * Copyright (c) 2013, Timothy Stack
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
 *
 * @file fs-extension-functions.cc
 */

#include <future>
#include <string>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/auto_fd.hh"
#include "base/auto_mem.hh"
#include "base/auto_pid.hh"
#include "base/injector.hh"
#include "base/lnav.console.hh"
#include "base/opt_util.hh"
#include "bound_tags.hh"
#include "config.h"
#include "sqlite-extension-func.hh"
#include "sqlite3.h"
#include "vtab_module.hh"
#include "yajlpp/yajlpp_def.hh"

static mapbox::util::variant<const char*, string_fragment>
sql_basename(const char* path_in)
{
    int text_end = -1;

    if (path_in[0] == '\0') {
        return ".";
    }

    for (ssize_t lpc = strlen(path_in) - 1; lpc >= 0; lpc--) {
        if (path_in[lpc] == '/' || path_in[lpc] == '\\') {
            if (text_end != -1) {
                return string_fragment(path_in, lpc + 1, text_end);
            }
        } else if (text_end == -1) {
            text_end = (int) (lpc + 1);
        }
    }

    if (text_end == -1) {
        return "/";
    } else {
        return string_fragment(path_in, 0, text_end);
    }
}

static mapbox::util::variant<const char*, string_fragment>
sql_dirname(const char* path_in)
{
    ssize_t text_end;

    text_end = strlen(path_in) - 1;
    while (text_end >= 0
           && (path_in[text_end] == '/' || path_in[text_end] == '\\'))
    {
        text_end -= 1;
    }

    while (text_end >= 0) {
        if (path_in[text_end] == '/' || path_in[text_end] == '\\') {
            return string_fragment(path_in, 0, text_end == 0 ? 1 : text_end);
        }

        text_end -= 1;
    }

    return path_in[0] == '/' ? "/" : ".";
}

static nonstd::optional<std::string>
sql_joinpath(const std::vector<const char*>& paths)
{
    std::string full_path;

    if (paths.empty()) {
        return nonstd::nullopt;
    }

    for (auto& path_in : paths) {
        if (path_in == nullptr) {
            return nonstd::nullopt;
        }

        if (path_in[0] == '/' || path_in[0] == '\\') {
            full_path.clear();
        }
        if (!full_path.empty() && full_path[full_path.length() - 1] != '/'
            && full_path[full_path.length() - 1] != '\\')
        {
            full_path += "/";
        }
        full_path += path_in;
    }

    return full_path;
}

static std::string
sql_readlink(const char* path)
{
    struct stat st;

    if (lstat(path, &st) == -1) {
        throw sqlite_func_error(
            "unable to stat path: {} -- {}", path, strerror(errno));
    }

    char buf[st.st_size];
    ssize_t rc;

    rc = readlink(path, buf, sizeof(buf));
    if (rc < 0) {
        if (errno == EINVAL) {
            return path;
        }
        throw sqlite_func_error(
            "unable to read link: {} -- {}", path, strerror(errno));
    }

    return std::string(buf, rc);
}

static std::string
sql_realpath(const char* path)
{
    char resolved_path[PATH_MAX];

    if (realpath(path, resolved_path) == nullptr) {
        throw sqlite_func_error(
            "Could not get real path for {} -- {}", path, strerror(errno));
    }

    return resolved_path;
}

struct shell_exec_options {
    std::map<std::string, nonstd::optional<std::string>> po_env;
};

static const json_path_container shell_exec_env_handlers = {
    yajlpp::pattern_property_handler(R"((?<name>[^=]+))")
        .for_field(&shell_exec_options::po_env),
};

static const typed_json_path_container<shell_exec_options>
    shell_exec_option_handlers = {
        yajlpp::property_handler("env").with_children(shell_exec_env_handlers),
};

static blob_auto_buffer
sql_shell_exec(const char* cmd,
               nonstd::optional<string_fragment> input,
               nonstd::optional<string_fragment> opts_json)
{
    static const intern_string_t SRC = intern_string::lookup("options");

    static auto& lnav_flags = injector::get<unsigned long&, lnav_flags_tag>();

    if (lnav_flags & LNF_SECURE_MODE) {
        throw sqlite_func_error("not available in secure mode");
    }

    shell_exec_options options;

    if (opts_json) {
        auto parse_res
            = shell_exec_option_handlers.parser_for(SRC).of(opts_json.value());

        if (parse_res.isErr()) {
            throw lnav::console::user_message::error(
                "invalid options parameter")
                .with_reason(parse_res.unwrapErr()[0]);
        }

        options = parse_res.unwrap();
    }

    auto child_fds_res
        = auto_pipe::for_child_fds(STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
    if (child_fds_res.isErr()) {
        throw lnav::console::user_message::error("cannot open child pipes")
            .with_reason(child_fds_res.unwrapErr());
    }
    auto child_pid_res = lnav::pid::from_fork();
    if (child_pid_res.isErr()) {
        throw lnav::console::user_message::error("cannot fork()")
            .with_reason(child_pid_res.unwrapErr());
    }

    auto child_fds = child_fds_res.unwrap();
    auto child_pid = child_pid_res.unwrap();

    for (auto& child_fd : child_fds) {
        child_fd.after_fork(child_pid.in());
    }

    if (child_pid.in_child()) {
        const char* args[] = {
            getenv_opt("SHELL").value_or("bash"),
            "-c",
            cmd,
            nullptr,
        };

        for (const auto& epair : options.po_env) {
            if (epair.second.has_value()) {
                setenv(epair.first.c_str(), epair.second->c_str(), 1);
            } else {
                unsetenv(epair.first.c_str());
            }
        }

        execvp(args[0], (char**) args);
        _exit(EXIT_FAILURE);
    }

    auto out_reader = std::async(
        std::launch::async, [out_fd = std::move(child_fds[1].read_end())]() {
            auto buffer = auto_buffer::alloc(4096);

            while (true) {
                if (buffer.available() < 4096) {
                    buffer.expand_by(4096);
                }

                auto rc
                    = read(out_fd, buffer.next_available(), buffer.available());
                if (rc < 0) {
                    break;
                }
                if (rc == 0) {
                    break;
                }
                buffer.resize_by(rc);
            }

            return buffer;
        });

    auto err_reader = std::async(
        std::launch::async, [err_fd = std::move(child_fds[2].read_end())]() {
            auto buffer = auto_buffer::alloc(4096);

            while (true) {
                if (buffer.available() < 4096) {
                    buffer.expand_by(4096);
                }

                auto rc
                    = read(err_fd, buffer.next_available(), buffer.available());
                if (rc < 0) {
                    break;
                }
                if (rc == 0) {
                    break;
                }
                buffer.resize_by(rc);
            }

            return buffer;
        });

    if (input) {
        child_fds[0].write_end().write_fully(input.value());
    }
    child_fds[0].close();

    auto retval = blob_auto_buffer{out_reader.get()};

    auto finished_child = std::move(child_pid).wait_for_child();

    if (!finished_child.was_normal_exit()) {
        throw sqlite_func_error("child failed with signal {}",
                                finished_child.term_signal());
    }

    if (finished_child.exit_status() != EXIT_SUCCESS) {
        throw lnav::console::user_message::error(
            attr_line_t("child failed with exit code ")
                .append(lnav::roles::number(
                    fmt::to_string(finished_child.exit_status()))))
            .with_reason(err_reader.get().to_string());
    }

    return retval;
}

int
fs_extension_functions(struct FuncDef** basic_funcs,
                       struct FuncDefAgg** agg_funcs)
{
    static struct FuncDef fs_funcs[] = {

        sqlite_func_adapter<decltype(&sql_basename), sql_basename>::builder(
            help_text("basename", "Extract the base portion of a pathname.")
                .sql_function()
                .with_prql_path({"fs", "basename"})
                .with_parameter({"path", "The path"})
                .with_tags({"filename"})
                .with_example({"To get the base of a plain file name",
                               "SELECT basename('foobar')"})
                .with_example(
                    {"To get the base of a path", "SELECT basename('foo/bar')"})
                .with_example({"To get the base of a directory",
                               "SELECT basename('foo/bar/')"})
                .with_example({"To get the base of an empty string",
                               "SELECT basename('')"})
                .with_example({"To get the base of a Windows path",
                               "SELECT basename('foo\\bar')"})
                .with_example({"To get the base of the root directory",
                               "SELECT basename('/')"})
                .with_example({
                    "To get the base of a path",
                    "from [{p='foo/bar'}] | select { fs.basename p }",
                    help_example::language::prql,
                })),

        sqlite_func_adapter<decltype(&sql_dirname), sql_dirname>::builder(
            help_text("dirname", "Extract the directory portion of a pathname.")
                .sql_function()
                .with_parameter({"path", "The path"})
                .with_prql_path({"fs", "dirname"})
                .with_tags({"filename"})
                .with_example({"To get the directory of a relative file path",
                               "SELECT dirname('foo/bar')"})
                .with_example({"To get the directory of an absolute file path",
                               "SELECT dirname('/foo/bar')"})
                .with_example(
                    {"To get the directory of a file in the root directory",
                     "SELECT dirname('/bar')"})
                .with_example({"To get the directory of a Windows path",
                               "SELECT dirname('foo\\bar')"})
                .with_example({"To get the directory of an empty path",
                               "SELECT dirname('')"})),

        sqlite_func_adapter<decltype(&sql_joinpath), sql_joinpath>::builder(
            help_text("joinpath", "Join components of a path together.")
                .sql_function()
                .with_prql_path({"fs", "join"})
                .with_parameter(
                    help_text(
                        "path",
                        "One or more path components to join together.  "
                        "If an argument starts with a forward or backward "
                        "slash, it will be considered "
                        "an absolute path and any preceding elements will "
                        "be ignored.")
                        .one_or_more())
                .with_tags({"filename"})
                .with_example(
                    {"To join a directory and file name into a relative path",
                     "SELECT joinpath('foo', 'bar')"})
                .with_example(
                    {"To join an empty component with other names into "
                     "a relative path",
                     "SELECT joinpath('', 'foo', 'bar')"})
                .with_example(
                    {"To create an absolute path with two path components",
                     "SELECT joinpath('/', 'foo', 'bar')"})
                .with_example(
                    {"To create an absolute path from a path component "
                     "that starts with a forward slash",
                     "SELECT joinpath('/', 'foo', '/bar')"})),

        sqlite_func_adapter<decltype(&sql_readlink), sql_readlink>::builder(
            help_text("readlink", "Read the target of a symbolic link.")
                .sql_function()
                .with_prql_path({"fs", "readlink"})
                .with_parameter({"path", "The path to the symbolic link."})
                .with_tags({"filename"})),

        sqlite_func_adapter<decltype(&sql_realpath), sql_realpath>::builder(
            help_text(
                "realpath",
                "Returns the resolved version of the given path, expanding "
                "symbolic links and "
                "resolving '.' and '..' references.")
                .sql_function()
                .with_prql_path({"fs", "realpath"})
                .with_parameter({"path", "The path to resolve."})
                .with_tags({"filename"})),

        sqlite_func_adapter<decltype(&sql_shell_exec), sql_shell_exec>::builder(
            help_text("shell_exec",
                      "Executes a shell command and returns its output.")
                .sql_function()
                .with_prql_path({"shell", "exec"})
                .with_parameter({"cmd", "The command to execute."})
                .with_parameter(help_text{
                    "input",
                    "A blob of data to write to the command's standard input."}
                                    .optional())
                .with_parameter(
                    help_text{"options",
                              "A JSON object containing options for the "
                              "execution with the following properties:"}
                        .optional()
                        .with_parameter(help_text{
                            "env",
                            "An object containing the environment variables "
                            "to set or, if NULL, to unset."}
                                            .optional()))
                .with_tags({"shell"}))
            .with_flags(
#ifdef SQLITE_DIRECTONLY
                SQLITE_DIRECTONLY |
#endif
                SQLITE_UTF8),

        /*
         * TODO: add other functions like normpath, ...
         */

        {nullptr},
    };

    *basic_funcs = fs_funcs;
    *agg_funcs = nullptr;

    return SQLITE_OK;
}
