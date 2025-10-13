/**
 * Copyright (c) 2025, Timothy Stack
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
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/itertools.hh"
#include "base/lnav.console.hh"
#include "base/lnav.ryml.hh"
#include "base/result.h"
#include "bound_tags.hh"
#include "command_executor.hh"
#include "config.h"
#include "external_opener.hh"
#include "libbase64.h"
#include "lnav.hh"
#include "lnav.indexing.hh"
#include "lnav.prompt.hh"
#include "lnav_commands.hh"
#include "md4c/md4c-html.h"
#include "md4cpp.hh"
#include "pcrepp/pcre2pp.hh"
#include "readline_context.hh"
#include "scn/scan.h"
#include "service_tags.hh"
#include "session.export.hh"
#include "shlex.hh"
#include "static-files.h"
#include "sysclip.hh"
#include "text_format.hh"
#include "top_status_source.hh"
#include "yajlpp/yajlpp.hh"

#ifdef HAVE_RUST_DEPS
#    include "lnav_rs_ext.cxx.hh"
#endif

using namespace lnav::roles::literals;
using namespace std::string_view_literals;

static Result<std::string, lnav::console::user_message>
com_export_session_to(exec_context& ec,
                      std::string cmdline,
                      std::vector<std::string>& args)
{
    std::string retval;

    if (!ec.ec_dry_run) {
        auto_mem<FILE> outfile(fclose);
        auto fn = trim(remaining_args(cmdline, args));
        auto to_term = false;

        if (fn == "-" || fn == "/dev/stdout") {
            auto ec_out = ec.get_output();

            if (!ec_out) {
                outfile = auto_mem<FILE>::leak(stdout);

                if (ec.ec_ui_callbacks.uc_pre_stdout_write) {
                    ec.ec_ui_callbacks.uc_pre_stdout_write();
                }
                setvbuf(stdout, nullptr, _IONBF, 0);
                to_term = true;
                fprintf(outfile,
                        "\n---------------- Press any key to exit "
                        "lo-fi "
                        "display "
                        "----------------\n\n");
            } else {
                outfile = auto_mem<FILE>::leak(ec_out.value());
            }
            if (outfile.in() == stdout) {
                lnav_data.ld_stdout_used = true;
            }
        } else if (fn == "/dev/clipboard") {
            auto open_res = sysclip::open(sysclip::type_t::GENERAL);
            if (open_res.isErr()) {
                alerter::singleton().chime("cannot open clipboard");
                return ec.make_error("Unable to copy to clipboard: {}",
                                     open_res.unwrapErr());
            }
            outfile = open_res.unwrap();
        } else if (lnav_data.ld_flags & LNF_SECURE_MODE) {
            return ec.make_error("{} -- unavailable in secure mode", args[0]);
        } else {
            if ((outfile = fopen(fn.c_str(), "we")) == nullptr) {
                return ec.make_error("unable to open file -- {}", fn);
            }
            fchmod(fileno(outfile.in()), S_IRWXU);
        }

        auto export_res = lnav::session::export_to(outfile.in());

        fflush(outfile.in());
        if (to_term) {
            if (ec.ec_ui_callbacks.uc_post_stdout_write) {
                ec.ec_ui_callbacks.uc_post_stdout_write();
            }
        }
        if (export_res.isErr()) {
            return Err(export_res.unwrapErr());
        }

        retval = fmt::format(
            FMT_STRING("info: wrote session commands to -- {}"), fn);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_rebuild(exec_context& ec,
            std::string cmdline,
            std::vector<std::string>& args)
{
    if (!ec.ec_dry_run) {
        rescan_files(true);
        rebuild_indexes_repeatedly();
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_echo(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    std::string retval = "error: expecting a message";

    if (args.size() >= 1) {
        bool lf = true;
        std::string src;

        if (args.size() > 2 && args[1] == "-n") {
            std::string::size_type index_in_cmdline = cmdline.find(args[1]);

            lf = false;
            src = cmdline.substr(index_in_cmdline + args[1].length() + 1);
        } else if (args.size() >= 2) {
            src = cmdline.substr(args[0].length() + 1);
        } else {
            src = "";
        }

        auto lexer = shlex(src);
        lexer.eval(retval, ec.create_resolver());

        auto ec_out = ec.get_output();
        if (ec.ec_dry_run) {
            lnav_data.ld_preview_status_source[0].get_description().set_value(
                "The text to output:"_frag);
            lnav_data.ld_status[LNS_PREVIEW0].set_needs_update();
            lnav_data.ld_preview_view[0].set_sub_source(
                &lnav_data.ld_preview_source[0]);
            lnav_data.ld_preview_source[0].replace_with(attr_line_t(retval));
            retval = "";
        } else if (ec_out) {
            FILE* outfile = *ec_out;

            if (outfile == stdout) {
                lnav_data.ld_stdout_used = true;
            }

            fprintf(outfile, "%s", retval.c_str());
            if (lf) {
                putc('\n', outfile);
            }
            fflush(outfile);

            retval = "";
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_alt_msg(exec_context& ec,
            std::string cmdline,
            std::vector<std::string>& args)
{
    static auto& prompt = lnav::prompt::get();

    std::string retval;

    if (ec.ec_dry_run) {
        retval = "";
    } else if (args.size() == 1) {
        prompt.p_editor.clear_alt_value();
        retval = "";
    } else {
        std::string msg = remaining_args(cmdline, args);

        prompt.p_editor.set_alt_value(msg);
        retval = "";
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_eval(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() > 1) {
        static intern_string_t EVAL_SRC = intern_string::lookup(":eval");

        std::string all_args = remaining_args(cmdline, args);
        std::string expanded_cmd;
        shlex lexer(all_args.c_str(), all_args.size());

        log_debug("Evaluating: %s", all_args.c_str());
        if (!lexer.eval(expanded_cmd,
                        {
                            &ec.ec_local_vars.top(),
                            &ec.ec_global_vars,
                        }))
        {
            return ec.make_error("invalid arguments");
        }
        log_debug("Expanded command to evaluate: %s", expanded_cmd.c_str());

        if (expanded_cmd.empty()) {
            return ec.make_error("empty result after evaluation");
        }

        if (ec.ec_dry_run) {
            attr_line_t al(expanded_cmd);

            lnav_data.ld_preview_status_source[0].get_description().set_value(
                "The command to be executed:"_frag);
            lnav_data.ld_status[LNS_PREVIEW0].set_needs_update();

            lnav_data.ld_preview_view[0].set_sub_source(
                &lnav_data.ld_preview_source[0]);
            lnav_data.ld_preview_source[0].replace_with(al);

            return Ok(std::string());
        }

        auto src_guard = ec.enter_source(EVAL_SRC, 1, expanded_cmd);
        auto content = string_fragment::from_str(expanded_cmd);
        multiline_executor me(ec, ":eval");
        for (auto line : content.split_lines()) {
            TRY(me.push_back(line));
        }
        TRY(me.final());
        retval = std::move(me.me_last_result);
    } else {
        return ec.make_error("expecting a command or query to evaluate");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_cd(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("path");

    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    }

    std::vector<std::string> word_exp;
    std::string pat;

    pat = trim(remaining_args(cmdline, args));

    shlex lexer(pat);
    auto split_args_res = lexer.split(ec.create_resolver());
    if (split_args_res.isErr()) {
        auto split_err = split_args_res.unwrapErr();
        auto um
            = lnav::console::user_message::error("unable to parse file name")
                  .with_reason(split_err.se_error.te_msg)
                  .with_snippet(lnav::console::snippet::from(
                      SRC, lexer.to_attr_line(split_err.se_error)))
                  .move();

        return Err(um);
    }

    auto split_args = split_args_res.unwrap()
        | lnav::itertools::map([](const auto& elem) { return elem.se_value; });

    if (split_args.size() != 1) {
        return ec.make_error("expecting a single argument");
    }

    struct stat st;

    if (stat(split_args[0].c_str(), &st) != 0) {
        return Err(ec.make_error_msg("cannot access -- {}", split_args[0])
                       .with_errno_reason());
    }

    if (!S_ISDIR(st.st_mode)) {
        return ec.make_error("{} is not a directory", split_args[0]);
    }

    if (!ec.ec_dry_run) {
        chdir(split_args[0].c_str());
        setenv("PWD", split_args[0].c_str(), 1);
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_sh(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    }

    static size_t EXEC_COUNT = 0;

    if (!ec.ec_dry_run) {
        std::optional<std::string> name_flag;

        shlex lexer(cmdline);
        auto cmd_start = args[0].size();
        auto split_res = lexer.split(ec.create_resolver());
        if (split_res.isOk()) {
            auto flags = split_res.unwrap();
            if (flags.size() >= 2) {
                static const char* NAME_FLAG = "--name=";

                if (startswith(flags[1].se_value, NAME_FLAG)) {
                    name_flag = flags[1].se_value.substr(strlen(NAME_FLAG));
                    cmd_start = flags[1].se_origin.sf_end;
                }
            }
        }

        auto carg = trim(cmdline.substr(cmd_start));

        log_info("executing: %s", carg.c_str());

        auto child_fds_res
            = auto_pipe::for_child_fds(STDOUT_FILENO, STDERR_FILENO);
        if (child_fds_res.isErr()) {
            auto um = lnav::console::user_message::error(
                          "unable to create child pipes")
                          .with_reason(child_fds_res.unwrapErr())
                          .move();
            ec.add_error_context(um);
            return Err(um);
        }
        auto child_res = lnav::pid::from_fork();
        if (child_res.isErr()) {
            auto um
                = lnav::console::user_message::error("unable to fork() child")
                      .with_reason(child_res.unwrapErr())
                      .move();
            ec.add_error_context(um);
            return Err(um);
        }

        auto child_fds = child_fds_res.unwrap();
        auto child = child_res.unwrap();
        for (auto& child_fd : child_fds) {
            child_fd.after_fork(child.in());
        }
        if (child.in_child()) {
            auto dev_null = open("/dev/null", O_RDONLY | O_CLOEXEC);

            dup2(dev_null, STDIN_FILENO);
            const char* exec_args[] = {
                getenv_opt("SHELL").value_or("bash"),
                "-c",
                carg.c_str(),
                nullptr,
            };

            for (const auto& pair : ec.ec_local_vars.top()) {
                pair.second.match(
                    [&pair](const std::string& val) {
                        setenv(pair.first.c_str(), val.c_str(), 1);
                    },
                    [&pair](const string_fragment& sf) {
                        setenv(pair.first.c_str(), sf.to_string().c_str(), 1);
                    },
                    [](null_value_t) {},
                    [&pair](int64_t val) {
                        setenv(
                            pair.first.c_str(), fmt::to_string(val).c_str(), 1);
                    },
                    [&pair](double val) {
                        setenv(
                            pair.first.c_str(), fmt::to_string(val).c_str(), 1);
                    },
                    [&pair](bool val) {
                        setenv(pair.first.c_str(), val ? "1" : "0", 1);
                    });
            }

            execvp(exec_args[0], (char**) exec_args);
            _exit(EXIT_FAILURE);
        }

        std::string display_name;
        auto open_prov = ec.get_provenance<exec_context::file_open>();
        if (open_prov) {
            if (name_flag) {
                display_name = fmt::format(
                    FMT_STRING("{}/{}"), open_prov->fo_name, name_flag.value());
            } else {
                display_name = open_prov->fo_name;
            }
        } else if (name_flag) {
            display_name = name_flag.value();
        } else {
            display_name
                = fmt::format(FMT_STRING("sh-{} {}"), EXEC_COUNT++, carg);
        }

        auto name_base = display_name;
        size_t name_counter = 0;

        while (true) {
            auto fn_iter
                = lnav_data.ld_active_files.fc_file_names.find(display_name);
            if (fn_iter == lnav_data.ld_active_files.fc_file_names.end()) {
                break;
            }
            name_counter += 1;
            display_name
                = fmt::format(FMT_STRING("{} [{}]"), name_base, name_counter);
        }

        auto create_piper_res
            = lnav::piper::create_looper(display_name,
                                         std::move(child_fds[0].read_end()),
                                         std::move(child_fds[1].read_end()));

        if (create_piper_res.isErr()) {
            auto um
                = lnav::console::user_message::error("unable to create piper")
                      .with_reason(create_piper_res.unwrapErr())
                      .move();
            ec.add_error_context(um);
            return Err(um);
        }

        lnav_data.ld_active_files.fc_file_names[display_name].with_piper(
            create_piper_res.unwrap());
        lnav_data.ld_child_pollers.emplace_back(child_poller{
            display_name,
            std::move(child),
            [](auto& fc, auto& child) {},
        });
        lnav_data.ld_files_to_front.emplace_back(display_name);

        return Ok(fmt::format(FMT_STRING("info: executing -- {}"), carg));
    }

    return Ok(std::string());
}

#ifdef HAVE_RUST_DEPS

static lnav::task_progress
ext_prog_rep()
{
    auto ext = lnav_rs_ext::get_status();
    auto status = ext.status == lnav_rs_ext::Status::idle
        ? lnav::progress_status_t::idle
        : lnav::progress_status_t::working;
    std::vector<lnav::console::user_message> msgs_out;
    for (const auto& err : ext.messages) {
        auto um = lnav::console::user_message::error((std::string) err.error)
                      .with_reason((std::string) err.source)
                      .with_help((std::string) err.help);
        msgs_out.emplace_back(um);
    }
    auto retval = lnav::task_progress{
        (std::string) ext.id,
        status,
        ext.version,
        (std::string) ext.current_step,
        ext.completed,
        ext.total,
        std::move(msgs_out),
    };

    return retval;
}

DIST_SLICE(prog_reps) lnav::progress_reporter_t EXT_PROG_REP = ext_prog_rep;

namespace lnav_rs_ext {

LnavLogLevel
get_lnav_log_level()
{
    switch (lnav_log_level) {
        case lnav_log_level_t::TRACE:
            return LnavLogLevel::trace;
        case lnav_log_level_t::DEBUG:
            return LnavLogLevel::debug;
        case lnav_log_level_t::INFO:
            return LnavLogLevel::info;
        case lnav_log_level_t::WARNING:
            return LnavLogLevel::warning;
        case lnav_log_level_t::ERROR:
            return LnavLogLevel::error;
    }

    return LnavLogLevel::info;
}

void
log_msg(LnavLogLevel level, ::rust::Str file, uint32_t line, ::rust::Str msg)
{
    auto ln_level = static_cast<lnav_log_level_t>(level);

    ::log_msg(ln_level,
              ((std::string) file).c_str(),
              line,
              "%.*s",
              msg.size(),
              msg.data());
}

::rust::String
version_info()
{
    yajlpp_gen gen;
    {
        yajlpp_map root(gen);

        root.gen("product");
        root.gen(PACKAGE);
        root.gen("version");
        root.gen(PACKAGE_VERSION);
    }

    return gen.to_string_fragment().to_string();
}

static void
process_md_out(const MD_CHAR* data, MD_SIZE len, void* user_data)
{
    auto& buffer = *((::rust::Vec<uint8_t>*) user_data);
    auto sv = std::string_view(data, len);

    std::copy(sv.begin(), sv.end(), std::back_inserter(buffer));
}

struct static_file_not_found_t {};

using static_file_t
    = mapbox::util::variant<const bin_src_file*, static_file_not_found_t>;

static std::optional<const bin_src_file*>
find_static_src_file(const std::string& path)
{
    for (const auto& file : lnav_static_files) {
        if (file.get_name() == path) {
            return &file;
        }
    }

    return std::nullopt;
}

static static_file_t
find_static_file(const std::string& path)
{
    auto exact_match = find_static_src_file(path);
    if (exact_match) {
        return static_file_t{exact_match.value()};
    }

    return static_file_t{static_file_not_found_t{}};
}

static static_file_t
resolve_static_src_file(std::string path)
{
    static const auto HTML_EXT = lnav::pcre2pp::code::from_const("\\.html$");

    auto exact_match = find_static_file(path);
    if (!exact_match.is<static_file_not_found_t>()) {
        return exact_match;
    }

    if (!path.empty() && !endswith(path, "/")) {
        path += "/";
    }
    path += "index.html";
    auto index_match = find_static_file(path);
    if (!index_match.is<static_file_not_found_t>()) {
        return index_match;
    }

    path = HTML_EXT.replace(path, ".md");
    auto md_match = find_static_file(path);
    if (!md_match.is<static_file_not_found_t>()) {
        return md_match;
    }

    return static_file_t{static_file_not_found_t{}};
}

static void
render_markdown(const bin_src_file& file, ::rust::Vec<uint8_t>& dst)
{
    static const auto HEADER = R"(
<html>
<head>
<title>{title}</title>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/prism/1.30.0/themes/prism.min.css">
</head>
<body>
)";
    static const auto FOOTER = R"(
<script src="https://cdnjs.cloudflare.com/ajax/libs/prism/1.30.0/prism.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/prism/1.30.0/components/prism-sql.min.js"></script>
<script src="/assets/js/prism-lnav.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/prism/1.30.0/plugins/autoloader/prism-autoloader.min.js"></script>
<script src="/assets/js/lnav-client.js"></script>
</body>
</html>
)"sv;

    auto content = file.to_string_fragment_producer()->to_string();
    auto md_file = md4cpp::parse_file(file.get_name().to_string(), content);
    auto title = fmt::format(FMT_STRING("lnav: {}"), file.get_name());

    if (md_file.f_frontmatter_format == text_format_t::TF_YAML) {
        auto tree = ryml::parse_in_arena(
            lnav::ryml::to_csubstr(file.get_name()),
            lnav::ryml::to_csubstr(md_file.f_frontmatter));
        tree["title"] >> title;
    }

    auto head = fmt::format(HEADER, fmt::arg("title", title));
    std::copy(head.begin(), head.end(), std::back_inserter(dst));
    md_html(md_file.f_body.data(),
            md_file.f_body.length(),
            process_md_out,
            &dst,
            MD_DIALECT_GITHUB | MD_FLAG_UNDERLINE | MD_FLAG_STRIKETHROUGH,
            0);
    std::copy(FOOTER.begin(), FOOTER.end(), std::back_inserter(dst));
}

void
get_static_file(::rust::Str path, ::rust::Vec<uint8_t>& dst)
{
    auto path_str = (std::string) path;

    if (startswith(path_str, "/")) {
        path_str.erase(0, 1);
    }
    log_info("static file request: %s", path_str.c_str());
    auto matched_file = resolve_static_src_file(path_str);
    matched_file.match(
        [&dst](const bin_src_file* file) {
            auto name = file->get_name();
            log_info("  matched static source file: %.*s",
                     name.length(),
                     name.data());
            if (name.endswith(".md")) {
                render_markdown(*file, dst);
            } else {
                auto prod = file->to_string_fragment_producer();
                prod->for_each([&dst](const auto& sf) {
                    std::copy(sf.begin(), sf.end(), std::back_inserter(dst));
                    return Ok();
                });
            }
        },
        [](const static_file_not_found_t&) {
            log_info("  static file not found");
        });
}

ExecResult
execute_external_command(::rust::String rs_src,
                         ::rust::String rs_script,
                         ::rust::String hdrs)
{
    auto src = (std::string) rs_src;
    auto script = (std::string) rs_script;
    auto retval = std::make_shared<ExecResult>();

    log_debug("sending remote command to main looper");
    isc::to<main_looper&, services::main_t>().send_and_wait(
        [src, script, hdrs, &retval](auto& mlooper) {
            log_debug("executing remote command from: %s", src.c_str());
            db_label_source ext_db_source;
            auto& ec = lnav_data.ld_exec_context;
            // XXX we should still allow an external command to update the
            // regular DB view.
            auto dsg = ec.enter_db_source(&ext_db_source);
            auto* outfile = tmpfile();
            auto ec_out = exec_context::output_t{outfile, fclose};
            auto og = exec_context::output_guard{ec, "default", ec_out};
            auto me = multiline_executor{ec, src};
            auto pg = ec.with_provenance(exec_context::external_access{src});
            ec.ec_local_vars.push(std::map<std::string, scoped_value_t>{
                {"headers", scoped_value_t{(std::string) hdrs}}});
            auto script_frag = string_fragment::from_str(script);
            for (const auto& line : script_frag.split_lines()) {
                auto res = me.push_back(line);
                if (res.isErr()) {
                    auto um = res.unwrapErr();
                    retval->error.msg = um.um_message.al_string;
                    retval->error.reason = um.um_reason.al_string;
                    retval->error.help = um.um_help.al_string;
                    ec.ec_local_vars.pop();
                    return;
                }
            }
            auto res = me.final();
            if (res.isErr()) {
                auto um = res.unwrapErr();
                retval->error.msg = um.um_message.al_string;
                retval->error.reason = um.um_reason.al_string;
                retval->error.help = um.um_help.al_string;
            } else {
                fseek(ec_out.first, 0, SEEK_SET);
                retval->status = me.me_last_result;
                retval->content_type = fmt::to_string(ec.get_output_format());
                retval->content_fd = dup(fileno(ec_out.first));
            }
            ec.ec_local_vars.pop();
        });

    return *retval;
}

}  // namespace lnav_rs_ext
#endif

static Result<std::string, lnav::console::user_message>
com_external_access(exec_context& ec,
                    std::string cmdline,
                    std::vector<std::string>& args)
{
#ifdef HAVE_RUST_DEPS
    if (args.size() != 3) {
        return ec.make_error("Expecting port number and API key");
    }

    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("External access is not available in secure mode");
    }

    std::string retval;
    if (ec.ec_dry_run) {
        return Ok(retval);
    }

    auto scan_res = scn::scan_int<uint16_t>(args[1]);
    if (!scan_res || !scan_res.value().range().empty()) {
        return ec.make_error(FMT_STRING("port value is not a number: {}"),
                             args[1]);
    }
    auto port = scan_res->value();

    auto buf = auto_buffer::alloc((args[2].size() * 5) / 3);
    auto outlen = buf.capacity();
    base64_encode(args[2].data(), args[2].size(), buf.in(), &outlen, 0);
    auto start_res
        = lnav_rs_ext::start_ext_access(port, ::rust::String(buf.in(), outlen));
    if (start_res.port == 0) {
        return ec.make_error(FMT_STRING("unable to start external access: {}"),
                             (std::string) start_res.error);
    }

    retval = fmt::format(FMT_STRING("info: started external access on port {}"),
                         start_res.port);
    setenv("LNAV_EXTERNAL_PORT", fmt::to_string(start_res.port).c_str(), 1);
    auto url = fmt::format(FMT_STRING("http://127.0.0.1:{}"), start_res.port);
    setenv("LNAV_EXTERNAL_URL", url.c_str(), 1);

    {
        auto top_source = injector::get<std::shared_ptr<top_status_source>>();

        auto& sf = top_source->statusview_value_for_field(
            top_status_source::TSF_EXT_ACCESS);

        sf.set_width(3);
        sf.set_value("\xF0\x9F\x8C\x90");
        sf.on_click = [](auto& top_source) {
            static const intern_string_t SRC
                = intern_string::lookup("internal");
            static const auto LOC = source_location{SRC};

            auto& ec = lnav_data.ld_exec_context;
            ec.execute(LOC, ":external-access-login");
        };
    }

    return Ok(retval);
#else
    return ec.make_error("lnav was compiled without Rust extensions");
#endif
}

static Result<std::string, lnav::console::user_message>
com_external_access_login(exec_context& ec,
                          std::string cmdline,
                          std::vector<std::string>& args)
{
#ifdef HAVE_RUST_DEPS
    auto url = getenv("LNAV_EXTERNAL_URL");
    if (url == nullptr) {
        auto um = lnav::console::user_message::error(
                      "external-access is not enabled")
                      .with_help(attr_line_t("Use the ")
                                     .append(":external-access"_keyword)
                                     .append(" command to enable"));

        return Err(um);
    }

    auto otp = (std::string) lnav_rs_ext::set_one_time_password();
    auto url_with_otp = fmt::format(FMT_STRING("{}/login?otp={}"), url, otp);
    auto open_res = lnav::external_opener::for_href(url_with_otp);
    if (open_res.isErr()) {
        auto err = open_res.unwrapErr();
        auto um = lnav::console::user_message::error(
                      "unable to open external access URL")
                      .with_reason(err);

        return Err(um);
    }

    return Ok(std::string());
#else
    return ec.make_error("lnav was compiled without Rust extensions");
#endif
}

static readline_context::command_t SCRIPTING_COMMANDS[] = {
    {
        "export-session-to",
        com_export_session_to,

        help_text(":export-session-to")
            .with_summary("Export the current lnav state to an executable lnav "
                          "script file that contains the commands needed to "
                          "restore the current session")
            .with_parameter(
                help_text("path", "The path to the file to write")
                    .with_format(help_parameter_format_t::HPF_LOCAL_FILENAME))
            .with_tags({"io", "scripting"}),
    },
    {
        "rebuild",
        com_rebuild,
        help_text(":rebuild")
            .with_summary("Forcefully rebuild file indexes")
            .with_tags({"scripting"}),
    },
    {
        "echo",
        com_echo,

        help_text(":echo")
            .with_summary(
                "Echo the given message to the screen or, if "
                ":redirect-to has "
                "been called, to output file specified in the "
                "redirect.  "
                "Variable substitution is performed on the message.  "
                "Use a "
                "backslash to escape any special characters, like '$'")
            .with_parameter(help_text("-n",
                                      "Do not print a line-feed at "
                                      "the end of the output")
                                .optional()
                                .with_format(help_parameter_format_t::HPF_TEXT))
            .with_parameter(help_text("msg", "The message to display"))
            .with_tags({"io", "scripting"})
            .with_example({"To output 'Hello, World!'", "Hello, World!"}),
    },
    {
        "alt-msg",
        com_alt_msg,

        help_text(":alt-msg")
            .with_summary("Display a message in the alternate command position")
            .with_parameter(help_text("msg", "The message to display")
                                .with_format(help_parameter_format_t::HPF_TEXT))
            .with_tags({"scripting"})
            .with_example({"To display 'Press t to switch to the text view' on "
                           "the bottom right",
                           "Press t to switch to the text view"}),
    },
    {
        "eval",
        com_eval,

        help_text(":eval")
            .with_summary("Evaluate the given command/query after doing "
                          "environment variable substitution")
            .with_parameter(help_text(
                "command", "The command or query to perform substitution on."))
            .with_tags({"scripting"})
            .with_examples({{"To substitute the table name from a variable",
                             ";SELECT * FROM ${table}"}}),
    },
    {
        "sh",
        com_sh,

        help_text(":sh")
            .with_summary("Execute the given command-line and display the "
                          "captured output")
            .with_parameter(help_text(
                "--name=<name>", "The name to give to the captured output"))
            .with_parameter(
                help_text("cmdline", "The command-line to execute."))
            .with_tags({"scripting"}),
    },
    {
        "cd",
        com_cd,

        help_text(":cd")
            .with_summary("Change the current directory")
            .with_parameter(
                help_text("dir", "The new current directory")
                    .with_format(help_parameter_format_t::HPF_DIRECTORY))
            .with_tags({"scripting"}),
    },
    {
        "external-access",
        com_external_access,
        help_text(":external-access")
            .with_summary(
                "Open a port to give remote access to this lnav instance")
            .with_parameter(
                help_text("port", "The port number to listen on")
                    .with_format(help_parameter_format_t::HPF_NUMBER))
            .with_parameter(
                help_text("api-key", "The API key")
                    .with_format(help_parameter_format_t::HPF_STRING))
            .with_tags({"scripting"}),
    },
    {
        "external-access-login",
        com_external_access_login,
        help_text(":external-access-login")
            .with_summary("Use the external-opener to open a URL that refers "
                          "to lnav's external-access server")
            .with_tags({"scripting"}),
    },
};

void
init_lnav_scripting_commands(readline_context::command_map_t& cmd_map)
{
    for (auto& cmd : SCRIPTING_COMMANDS) {
        cmd.c_help.index_tags();
        cmd_map[cmd.c_name] = &cmd;
    }
}
