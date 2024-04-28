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

#include "lnav.management_cli.hh"

#include "base/fs_util.hh"
#include "base/humanize.hh"
#include "base/humanize.time.hh"
#include "base/itertools.hh"
#include "base/paths.hh"
#include "base/result.h"
#include "base/string_util.hh"
#include "file_options.hh"
#include "fmt/chrono.h"
#include "fmt/format.h"
#include "itertools.similar.hh"
#include "lnav.hh"
#include "lnav_config.hh"
#include "log_format.hh"
#include "log_format_ext.hh"
#include "mapbox/variant.hpp"
#include "piper.looper.hh"
#include "regex101.import.hh"
#include "session_data.hh"

using namespace lnav::roles::literals;

namespace lnav {

namespace management {

struct no_subcmd_t {
    CLI::App* ns_root_app{nullptr};
};

static auto DEFAULT_WRAPPING
    = text_wrap_settings{}.with_padding_indent(4).with_width(60);

inline attr_line_t&
symbol_reducer(const std::string& elem, attr_line_t& accum)
{
    if (!accum.empty()) {
        accum.append(", ");
    }
    return accum.append(lnav::roles::symbol(elem));
}

inline attr_line_t&
subcmd_reducer(const CLI::App* app, attr_line_t& accum)
{
    return accum.append("\n ")
        .append("\u2022"_list_glyph)
        .append(" ")
        .append(lnav::roles::keyword(app->get_name()))
        .append(": ")
        .append(app->get_description());
}

struct subcmd_config_t {
    using action_t = std::function<perform_result_t(const subcmd_config_t&)>;

    CLI::App* sc_config_app{nullptr};
    action_t sc_action;
    std::string sc_path;

    static perform_result_t default_action(const subcmd_config_t& sc)
    {
        auto um = console::user_message::error(
            "expecting an operation related to the regex101.com integration");
        um.with_help(
            sc.sc_config_app->get_subcommands({})
            | lnav::itertools::fold(
                subcmd_reducer, attr_line_t{"the available operations are:"}));

        return {um};
    }

    static perform_result_t get_action(const subcmd_config_t&)
    {
        auto config_str = dump_config();
        auto um = console::user_message::raw(config_str);

        return {um};
    }

    static perform_result_t blame_action(const subcmd_config_t&)
    {
        auto blame = attr_line_t();

        for (const auto& pair : lnav_config_locations) {
            blame.appendf(FMT_STRING("{} -> {}:{}\n"),
                          pair.first,
                          pair.second.sl_source,
                          pair.second.sl_line_number);
        }

        auto um = console::user_message::raw(blame.rtrim());

        return {um};
    }

    static perform_result_t file_options_action(const subcmd_config_t& sc)
    {
        auto& safe_options_hier
            = injector::get<lnav::safe_file_options_hier&>();

        if (sc.sc_path.empty()) {
            auto um = lnav::console::user_message::error(
                "Expecting a file path to check for options");

            return {um};
        }

        safe::ReadAccess<lnav::safe_file_options_hier> options_hier(
            safe_options_hier);

        auto realpath_res = lnav::filesystem::realpath(sc.sc_path);
        if (realpath_res.isErr()) {
            auto um = lnav::console::user_message::error(
                          attr_line_t("Unable to get full path for file: ")
                              .append(lnav::roles::file(sc.sc_path)))
                          .with_reason(realpath_res.unwrapErr());

            return {um};
        }
        auto full_path = realpath_res.unwrap();
        auto file_opts = options_hier->match(full_path);
        if (file_opts) {
            auto content = attr_line_t().append(
                file_opts->second.to_json_string().to_string_fragment());
            auto um = lnav::console::user_message::raw(content);
            perform_result_t retval;

            retval.emplace_back(um);

            return retval;
        }

        auto um
            = lnav::console::user_message::info(
                  attr_line_t("no options found for file: ")
                      .append(lnav::roles::file(full_path.string())))
                  .with_help(
                      attr_line_t("Use the ")
                          .append(":set-file-timezone"_symbol)
                          .append(
                              " command to set the zone for messages in files "
                              "that do not include a zone in the timestamp"));

        return {um};
    }

    subcmd_config_t& set_action(action_t act)
    {
        if (!this->sc_action) {
            this->sc_action = std::move(act);
        }
        return *this;
    }
};

struct subcmd_format_t {
    using action_t = std::function<perform_result_t(const subcmd_format_t&)>;

    CLI::App* sf_format_app{nullptr};
    std::string sf_name;
    CLI::App* sf_regex_app{nullptr};
    std::string sf_regex_name;
    CLI::App* sf_regex101_app{nullptr};
    action_t sf_action;

    subcmd_format_t& set_action(action_t act)
    {
        if (!this->sf_action) {
            this->sf_action = std::move(act);
        }
        return *this;
    }

    Result<std::shared_ptr<log_format>, console::user_message> validate_format()
        const
    {
        if (this->sf_name.empty()) {
            auto um = console::user_message::error(
                "expecting a format name to operate on");
            um.with_note(
                (log_format::get_root_formats()
                 | lnav::itertools::map(&log_format::get_name)
                 | lnav::itertools::sort_with(intern_string_t::case_lt)
                 | lnav::itertools::map(&intern_string_t::to_string)
                 | lnav::itertools::fold(symbol_reducer, attr_line_t{}))
                    .add_header("the available formats are: ")
                    .wrap_with(&DEFAULT_WRAPPING));

            return Err(um);
        }

        auto lformat = log_format::find_root_format(this->sf_name.c_str());
        if (lformat == nullptr) {
            auto um = console::user_message::error(
                attr_line_t("unknown format: ")
                    .append(lnav::roles::symbol(this->sf_name)));
            um.with_note(
                (log_format::get_root_formats()
                 | lnav::itertools::map(&log_format::get_name)
                 | lnav::itertools::similar_to(this->sf_name)
                 | lnav::itertools::map(&intern_string_t::to_string)
                 | lnav::itertools::fold(symbol_reducer, attr_line_t{}))
                    .add_header("did you mean one of the following?\n")
                    .wrap_with(&DEFAULT_WRAPPING));

            return Err(um);
        }

        return Ok(lformat);
    }

    Result<external_log_format*, console::user_message>
    validate_external_format() const
    {
        auto lformat = TRY(this->validate_format());
        auto* ext_lformat = dynamic_cast<external_log_format*>(lformat.get());

        if (ext_lformat == nullptr) {
            return Err(console::user_message::error(
                attr_line_t()
                    .append_quoted(lnav::roles::symbol(this->sf_name))
                    .append(" is an internal format that is not defined in a "
                            "configuration file")));
        }

        return Ok(ext_lformat);
    }

    Result<std::pair<external_log_format*,
                     std::shared_ptr<external_log_format::pattern>>,
           console::user_message>
    validate_regex() const
    {
        auto* ext_lformat = TRY(this->validate_external_format());

        if (this->sf_regex_name.empty()) {
            auto um = console::user_message::error(
                "expecting a regex name to operate on");
            um.with_note(
                ext_lformat->elf_pattern_order
                | lnav::itertools::map(&external_log_format::pattern::p_name)
                | lnav::itertools::map(&intern_string_t::to_string)
                | lnav::itertools::fold(
                    symbol_reducer,
                    attr_line_t{"the available regexes are: "}));

            return Err(um);
        }

        for (const auto& pat : ext_lformat->elf_pattern_order) {
            if (pat->p_name == this->sf_regex_name) {
                return Ok(std::make_pair(ext_lformat, pat));
            }
        }

        auto um = console::user_message::error(
            attr_line_t("unknown regex: ")
                .append(lnav::roles::symbol(this->sf_regex_name)));
        um.with_note(
            (ext_lformat->elf_pattern_order
             | lnav::itertools::map(&external_log_format::pattern::p_name)
             | lnav::itertools::map(&intern_string_t::to_string)
             | lnav::itertools::similar_to(this->sf_regex_name)
             | lnav::itertools::fold(symbol_reducer, attr_line_t{}))
                .add_header("did you mean one of the following?\n"));

        return Err(um);
    }

    static perform_result_t default_action(const subcmd_format_t& sf)
    {
        auto validate_res = sf.validate_format();
        if (validate_res.isErr()) {
            return {validate_res.unwrapErr()};
        }

        auto lformat = validate_res.unwrap();
        auto* ext_format = dynamic_cast<external_log_format*>(lformat.get());

        attr_line_t ext_details;
        if (ext_format != nullptr) {
            ext_details.append("\n   ")
                .append("Regexes"_h3)
                .append(": ")
                .join(ext_format->elf_pattern_order
                          | lnav::itertools::map(
                              &external_log_format::pattern::p_name)
                          | lnav::itertools::map(&intern_string_t::to_string),
                      VC_ROLE.value(role_t::VCR_SYMBOL),
                      ", ");
        }

        auto um = console::user_message::error(
            attr_line_t("expecting an operation to perform on the ")
                .append(lnav::roles::symbol(sf.sf_name))
                .append(" format"));
        um.with_note(attr_line_t()
                         .append(lnav::roles::symbol(sf.sf_name))
                         .append(": ")
                         .append(lformat->lf_description)
                         .append(ext_details));
        um.with_help(
            sf.sf_format_app->get_subcommands({})
            | lnav::itertools::fold(
                subcmd_reducer, attr_line_t{"the available operations are:"}));

        return {um};
    }

    static perform_result_t default_regex_action(const subcmd_format_t& sf)
    {
        auto validate_res = sf.validate_regex();

        if (validate_res.isErr()) {
            return {validate_res.unwrapErr()};
        }

        auto um = console::user_message::error(
            attr_line_t("expecting an operation to perform on the ")
                .append(lnav::roles::symbol(sf.sf_regex_name))
                .append(" regular expression"));

        um.with_help(attr_line_t{"the available subcommands are:"}.append(
            sf.sf_regex_app->get_subcommands({})
            | lnav::itertools::fold(subcmd_reducer, attr_line_t{})));

        return {um};
    }

    static perform_result_t get_action(const subcmd_format_t& sf)
    {
        auto validate_res = sf.validate_format();

        if (validate_res.isErr()) {
            return {validate_res.unwrapErr()};
        }

        auto format = validate_res.unwrap();

        auto um = console::user_message::raw(
            attr_line_t()
                .append(lnav::roles::symbol(sf.sf_name))
                .append(": ")
                .append(on_blank(format->lf_description, "<no description>")));

        return {um};
    }

    static perform_result_t source_action(const subcmd_format_t& sf)
    {
        auto validate_res = sf.validate_external_format();

        if (validate_res.isErr()) {
            return {validate_res.unwrapErr()};
        }

        auto* format = validate_res.unwrap();

        if (format->elf_format_source_order.empty()) {
            return {
                console::user_message::error(
                    "format is builtin, there is no source file"),
            };
        }

        auto um = console::user_message::raw(
            format->elf_format_source_order[0].string());

        return {um};
    }

    static perform_result_t sources_action(const subcmd_format_t& sf)
    {
        auto validate_res = sf.validate_external_format();

        if (validate_res.isErr()) {
            return {validate_res.unwrapErr()};
        }

        auto* format = validate_res.unwrap();

        if (format->elf_format_source_order.empty()) {
            return {
                console::user_message::error(
                    "format is builtin, there is no source file"),
            };
        }

        auto um = console::user_message::raw(
            attr_line_t().join(format->elf_format_source_order,
                               VC_ROLE.value(role_t::VCR_TEXT),
                               "\n"));

        return {um};
    }

    static perform_result_t regex101_pull_action(const subcmd_format_t& sf)
    {
        auto validate_res = sf.validate_regex();
        if (validate_res.isErr()) {
            return {validate_res.unwrapErr()};
        }

        auto format_regex_pair = validate_res.unwrap();
        auto get_meta_res
            = lnav::session::regex101::get_entry(sf.sf_name, sf.sf_regex_name);

        return get_meta_res.match(
            [&sf](
                const lnav::session::regex101::error& err) -> perform_result_t {
                return {
                    console::user_message::error(
                        attr_line_t("unable to get DB entry for: ")
                            .append(lnav::roles::symbol(sf.sf_name))
                            .append("/")
                            .append(lnav::roles::symbol(sf.sf_regex_name)))
                        .with_reason(err.e_msg),
                };
            },
            [&sf](
                const lnav::session::regex101::no_entry&) -> perform_result_t {
                return {
                    console::user_message::error(
                        attr_line_t("regex ")
                            .append_quoted(
                                lnav::roles::symbol(sf.sf_regex_name))
                            .append(" of format ")
                            .append_quoted(lnav::roles::symbol(sf.sf_name))
                            .append(" has not been pushed to regex101.com"))
                        .with_help(
                            attr_line_t("use the ")
                                .append_quoted("push"_keyword)
                                .append(" subcommand to create the regex on "
                                        "regex101.com for easy editing")),
                };
            },
            [&](const lnav::session::regex101::entry& en) -> perform_result_t {
                auto retrieve_res = regex101::client::retrieve(en.re_permalink);

                return retrieve_res.match(
                    [&](const console::user_message& um) -> perform_result_t {
                        return {
                            console::user_message::error(
                                attr_line_t("unable to retrieve entry ")
                                    .append_quoted(
                                        lnav::roles::symbol(en.re_permalink))
                                    .append(" from regex101.com"))
                                .with_reason(um),
                        };
                    },
                    [&](const regex101::client::no_entry&) -> perform_result_t {
                        lnav::session::regex101::delete_entry(sf.sf_name,
                                                              sf.sf_regex_name);
                        return {
                            console::user_message::error(
                                attr_line_t("entry ")
                                    .append_quoted(
                                        lnav::roles::symbol(en.re_permalink))
                                    .append(
                                        " no longer exists on regex101.com"))
                                .with_help(attr_line_t("use the ")
                                               .append_quoted("delete"_keyword)
                                               .append(" subcommand to delete "
                                                       "the association")),
                        };
                    },
                    [&](const regex101::client::entry& remote_entry)
                        -> perform_result_t {
                        auto curr_entry = regex101::convert_format_pattern(
                            format_regex_pair.first, format_regex_pair.second);

                        if (curr_entry.e_regex == remote_entry.e_regex) {
                            return {
                                console::user_message::ok(
                                    attr_line_t("local regex is in sync "
                                                "with entry ")
                                        .append_quoted(lnav::roles::symbol(
                                            en.re_permalink))
                                        .append(" on regex101.com"))
                                    .with_help(
                                        attr_line_t("make edits on ")
                                            .append_quoted(lnav::roles::file(
                                                regex101::client::to_edit_url(
                                                    en.re_permalink)))
                                            .append(" and then run this "
                                                    "command again to update "
                                                    "the local values")),
                            };
                        }

                        auto patch_res
                            = regex101::patch(format_regex_pair.first,
                                              sf.sf_regex_name,
                                              remote_entry);

                        if (patch_res.isErr()) {
                            return {
                                console::user_message::error(
                                    attr_line_t(
                                        "unable to patch format regex: ")
                                        .append(lnav::roles::symbol(sf.sf_name))
                                        .append("/")
                                        .append(lnav::roles::symbol(
                                            sf.sf_regex_name)))
                                    .with_reason(patch_res.unwrapErr()),
                            };
                        }

                        auto um = console::user_message::ok(
                            attr_line_t("format patch file written to: ")
                                .append(lnav::roles::file(
                                    patch_res.unwrap().string())));
                        if (!format_regex_pair.first->elf_builtin_format) {
                            um.with_help(
                                attr_line_t("once the regex has been found "
                                            "to be working correctly, move the "
                                            "contents of the patch file to the "
                                            "original file at:\n   ")
                                    .append(lnav::roles::file(
                                        format_regex_pair.first
                                            ->elf_format_source_order.front()
                                            .string())));
                        }

                        return {um};
                    });
            });
    }

    static perform_result_t regex101_default_action(const subcmd_format_t& sf)
    {
        auto validate_res = sf.validate_regex();

        if (validate_res.isErr()) {
            return {validate_res.unwrapErr()};
        }

        auto um = console::user_message::error(
            attr_line_t("expecting an operation to perform on the ")
                .append(lnav::roles::symbol(sf.sf_regex_name))
                .append(" regex using regex101.com"));

        auto get_res
            = lnav::session::regex101::get_entry(sf.sf_name, sf.sf_regex_name);
        if (get_res.is<lnav::session::regex101::entry>()) {
            auto local_entry = get_res.get<lnav::session::regex101::entry>();
            um.with_note(
                attr_line_t("this regex is currently associated with the "
                            "following regex101.com entry:\n   ")
                    .append(lnav::roles::file(regex101::client::to_edit_url(
                        local_entry.re_permalink))));
        }

        um.with_help(attr_line_t{"the available subcommands are:"}.append(
            sf.sf_regex101_app->get_subcommands({})
            | lnav::itertools::fold(subcmd_reducer, attr_line_t{})));

        return {um};
    }

    static perform_result_t regex101_push_action(const subcmd_format_t& sf)
    {
        auto validate_res = sf.validate_regex();
        if (validate_res.isErr()) {
            return {validate_res.unwrapErr()};
        }

        auto format_regex_pair = validate_res.unwrap();
        auto entry = regex101::convert_format_pattern(format_regex_pair.first,
                                                      format_regex_pair.second);
        auto get_meta_res
            = lnav::session::regex101::get_entry(sf.sf_name, sf.sf_regex_name);

        if (get_meta_res.is<lnav::session::regex101::entry>()) {
            auto entry_meta
                = get_meta_res.get<lnav::session::regex101::entry>();
            auto retrieve_res
                = regex101::client::retrieve(entry_meta.re_permalink);

            if (retrieve_res.is<regex101::client::entry>()) {
                auto remote_entry = retrieve_res.get<regex101::client::entry>();

                if (remote_entry == entry) {
                    return {
                        console::user_message::ok(
                            attr_line_t("regex101 entry ")
                                .append(lnav::roles::symbol(
                                    entry_meta.re_permalink))
                                .append(" is already up-to-date")),
                    };
                }
            } else if (retrieve_res.is<console::user_message>()) {
                return {
                    retrieve_res.get<console::user_message>(),
                };
            }

            entry.e_permalink_fragment = entry_meta.re_permalink;
        }

        auto upsert_res = regex101::client::upsert(entry);
        auto upsert_info = upsert_res.unwrap();

        if (get_meta_res.is<lnav::session::regex101::no_entry>()) {
            lnav::session::regex101::insert_entry({
                format_regex_pair.first->get_name().to_string(),
                format_regex_pair.second->p_name.to_string(),
                upsert_info.cr_permalink_fragment,
                upsert_info.cr_delete_code,
            });
        }

        return {
            console::user_message::ok(
                attr_line_t("pushed regex to -- ")
                    .append(lnav::roles::file(regex101::client::to_edit_url(
                        upsert_info.cr_permalink_fragment))))
                .with_help(attr_line_t("use the ")
                               .append_quoted("pull"_keyword)
                               .append(" subcommand to update the format after "
                                       "you make changes on regex101.com")),
        };
    }

    static perform_result_t regex101_delete_action(const subcmd_format_t& sf)
    {
        auto get_res
            = lnav::session::regex101::get_entry(sf.sf_name, sf.sf_regex_name);

        return get_res.match(
            [&sf](
                const lnav::session::regex101::entry& en) -> perform_result_t {
                {
                    auto validate_res = sf.validate_external_format();

                    if (validate_res.isOk()) {
                        auto ppath = regex101::patch_path(validate_res.unwrap(),
                                                          en.re_permalink);

                        if (ghc::filesystem::exists(ppath)) {
                            return {
                                console::user_message::error(
                                    attr_line_t("cannot delete regex101 entry "
                                                "while patch file exists"))
                                    .with_note(attr_line_t("  ").append(
                                        lnav::roles::file(ppath.string())))
                                    .with_help(attr_line_t(
                                        "move the contents of the patch file "
                                        "to the main log format and then "
                                        "delete the file to continue")),
                            };
                        }
                    }
                }

                perform_result_t retval;
                if (en.re_delete_code.empty()) {
                    retval.emplace_back(
                        console::user_message::warning(
                            attr_line_t("not deleting regex101 entry ")
                                .append_quoted(
                                    lnav::roles::symbol(en.re_permalink)))
                            .with_reason(
                                "delete code is not known for this entry")
                            .with_note(
                                "formats created by importing a regex101.com "
                                "entry will not have a delete code"));
                } else {
                    auto delete_res
                        = regex101::client::delete_entry(en.re_delete_code);

                    if (delete_res.isErr()) {
                        return {
                            console::user_message::error(
                                "unable to delete regex101 entry")
                                .with_reason(delete_res.unwrapErr()),
                        };
                    }
                }

                lnav::session::regex101::delete_entry(sf.sf_name,
                                                      sf.sf_regex_name);

                retval.emplace_back(console::user_message::ok(
                    attr_line_t("deleted regex101 entry: ")
                        .append(lnav::roles::symbol(en.re_permalink))));

                return retval;
            },
            [&sf](
                const lnav::session::regex101::no_entry&) -> perform_result_t {
                return {
                    console::user_message::error(
                        attr_line_t("no regex101 entry for ")
                            .append(lnav::roles::symbol(sf.sf_name))
                            .append("/")
                            .append(lnav::roles::symbol(sf.sf_regex_name))),
                };
            },
            [&sf](
                const lnav::session::regex101::error& err) -> perform_result_t {
                return {
                    console::user_message::error(
                        attr_line_t("unable to get regex101 entry for ")
                            .append(lnav::roles::symbol(sf.sf_name))
                            .append("/")
                            .append(lnav::roles::symbol(sf.sf_regex_name)))
                        .with_reason(err.e_msg),
                };
            });
    }
};

struct subcmd_piper_t {
    using action_t = std::function<perform_result_t(const subcmd_piper_t&)>;

    CLI::App* sp_app{nullptr};
    action_t sp_action;

    subcmd_piper_t& set_action(action_t act)
    {
        if (!this->sp_action) {
            this->sp_action = std::move(act);
        }
        return *this;
    }

    static perform_result_t default_action(const subcmd_piper_t& sp)
    {
        auto um = console::user_message::error(
            "expecting an operation related to piper storage");
        um.with_help(
            sp.sp_app->get_subcommands({})
            | lnav::itertools::fold(
                subcmd_reducer, attr_line_t{"the available operations are:"}));

        return {um};
    }

    static perform_result_t list_action(const subcmd_piper_t&)
    {
        static const intern_string_t SRC = intern_string::lookup("piper");
        static const auto DOT_HEADER = ghc::filesystem::path(".header");

        struct item {
            lnav::piper::header i_header;
            std::string i_url;
            file_size_t i_total_size{0};
        };

        file_size_t grand_total{0};
        std::vector<item> items;
        std::error_code ec;

        for (const auto& instance_dir : ghc::filesystem::directory_iterator(
                 lnav::piper::storage_path(), ec))
        {
            if (!instance_dir.is_directory()) {
                log_warning("piper directory entry is not a directory: %s",
                            instance_dir.path().c_str());
                continue;
            }

            std::optional<lnav::piper::header> hdr_opt;
            auto url = fmt::format(FMT_STRING("piper://{}"),
                                   instance_dir.path().filename().string());
            file_size_t total_size{0};
            auto hdr_path = instance_dir / DOT_HEADER;
            if (ghc::filesystem::exists(hdr_path)) {
                auto hdr_read_res = lnav::filesystem::read_file(hdr_path);
                if (hdr_read_res.isOk()) {
                    auto parse_res
                        = lnav::piper::header_handlers.parser_for(SRC).of(
                            hdr_read_res.unwrap());
                    if (parse_res.isOk()) {
                        hdr_opt = parse_res.unwrap();
                    } else {
                        log_error("failed to parse header: %s -- %s",
                                  hdr_path.c_str(),
                                  parse_res.unwrapErr()[0]
                                      .to_attr_line()
                                      .get_string()
                                      .c_str());
                    }
                } else {
                    log_error("failed to read header file: %s -- %s",
                              hdr_path.c_str(),
                              hdr_read_res.unwrapErr().c_str());
                }
            }

            for (const auto& entry :
                 ghc::filesystem::directory_iterator(instance_dir.path()))
            {
                if (entry.path().filename() == DOT_HEADER) {
                    continue;
                }

                total_size += entry.file_size();
                char buffer[lnav::piper::HEADER_SIZE];

                auto entry_open_res
                    = lnav::filesystem::open_file(entry.path(), O_RDONLY);
                if (entry_open_res.isErr()) {
                    log_warning("unable to open piper file: %s -- %s",
                                entry.path().c_str(),
                                entry_open_res.unwrapErr().c_str());
                    continue;
                }

                auto entry_fd = entry_open_res.unwrap();
                if (read(entry_fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
                    log_warning("piper file is too small: %s",
                                entry.path().c_str());
                    continue;
                }
                auto hdr_bits_opt = lnav::piper::read_header(entry_fd, buffer);
                if (!hdr_bits_opt) {
                    log_warning("could not read piper header: %s",
                                entry.path().c_str());
                    continue;
                }

                auto hdr_buf = std::move(hdr_bits_opt.value());

                total_size -= hdr_buf.size();
                auto hdr_sf
                    = string_fragment::from_bytes(hdr_buf.in(), hdr_buf.size());
                auto hdr_parse_res
                    = lnav::piper::header_handlers.parser_for(SRC).of(hdr_sf);
                if (hdr_parse_res.isErr()) {
                    log_error("failed to parse piper header: %s",
                              hdr_parse_res.unwrapErr()[0]
                                  .to_attr_line()
                                  .get_string()
                                  .c_str());
                    continue;
                }

                auto hdr = hdr_parse_res.unwrap();

                if (!hdr_opt || hdr < hdr_opt.value()) {
                    hdr_opt = hdr;
                }
            }

            if (hdr_opt) {
                items.emplace_back(item{hdr_opt.value(), url, total_size});
            }

            grand_total += total_size;
        }

        if (ec && ec.value() != ENOENT) {
            auto um = lnav::console::user_message::error(
                          attr_line_t("unable to access piper directory: ")
                              .append(lnav::roles::file(
                                  lnav::piper::storage_path().string())))
                          .with_reason(ec.message());
            return {um};
        }

        if (items.empty()) {
            if (verbosity != verbosity_t::quiet) {
                auto um
                    = lnav::console::user_message::info(
                          attr_line_t("no piper captures were found in:\n\t")
                              .append(lnav::roles::file(
                                  lnav::piper::storage_path().string())))
                          .with_help(
                              attr_line_t("You can create a capture by "
                                          "piping data into ")
                                  .append(lnav::roles::file("lnav"))
                                  .append(" or using the ")
                                  .append_quoted(lnav::roles::symbol(":sh"))
                                  .append(" command"));
                return {um};
            }

            return {};
        }

        auto txt
            = items
            | lnav::itertools::sort_with([](const item& lhs, const item& rhs) {
                  if (lhs.i_header < rhs.i_header) {
                      return true;
                  }

                  if (rhs.i_header < lhs.i_header) {
                      return false;
                  }

                  return lhs.i_url < rhs.i_url;
              })
            | lnav::itertools::map([](const item& it) {
                  auto ago = humanize::time::point::from_tv(it.i_header.h_ctime)
                                 .as_time_ago();
                  auto retval = attr_line_t()
                                    .append(lnav::roles::list_glyph(
                                        fmt::format(FMT_STRING("{:>18}"), ago)))
                                    .append("  ")
                                    .append(lnav::roles::file(it.i_url))
                                    .append(" ")
                                    .append(lnav::roles::number(fmt::format(
                                        FMT_STRING("{:>8}"),
                                        humanize::file_size(
                                            it.i_total_size,
                                            humanize::alignment::columnar))))
                                    .append(" ")
                                    .append_quoted(lnav::roles::comment(
                                        it.i_header.h_name))
                                    .append("\n");
                  if (verbosity == verbosity_t::verbose) {
                      auto env_al
                          = it.i_header.h_env
                          | lnav::itertools::map([](const auto& pair) {
                                return attr_line_t()
                                    .append(lnav::roles::identifier(pair.first))
                                    .append("=")
                                    .append(pair.second)
                                    .append("\n");
                            })
                          | lnav::itertools::fold(
                                [](const auto& elem, auto& accum) {
                                    if (!accum.empty()) {
                                        accum.append(28, ' ');
                                    }
                                    return accum.append(elem);
                                },
                                attr_line_t());

                      retval.append(23, ' ')
                          .append("cwd: ")
                          .append(lnav::roles::file(it.i_header.h_cwd))
                          .append("\n")
                          .append(23, ' ')
                          .append("env: ")
                          .append(env_al);
                  }
                  return retval;
              })
            | lnav::itertools::fold(
                  [](const auto& elem, auto& accum) {
                      return accum.append(elem);
                  },
                  attr_line_t{});
        txt.rtrim();

        perform_result_t retval;
        if (verbosity != verbosity_t::quiet) {
            auto extra_um
                = lnav::console::user_message::info(
                      attr_line_t(
                          "the following piper captures were found in:\n\t")
                          .append(lnav::roles::file(
                              lnav::piper::storage_path().string())))
                      .with_note(
                          attr_line_t("The captures currently consume ")
                              .append(lnav::roles::number(humanize::file_size(
                                  grand_total, humanize::alignment::none)))
                              .append(" of disk space.  File sizes include "
                                      "associated metadata."))
                      .with_help(
                          "You can reopen a capture by passing the piper URL "
                          "to lnav");
            retval.emplace_back(extra_um);
        }
        retval.emplace_back(lnav::console::user_message::raw(txt));

        return retval;
    }

    static perform_result_t clean_action(const subcmd_piper_t&)
    {
        std::error_code ec;

        ghc::filesystem::remove_all(lnav::piper::storage_path(), ec);
        if (ec) {
            return {
                lnav::console::user_message::error(
                    "unable to remove piper storage directory")
                    .with_reason(ec.message()),
            };
        }

        return {};
    }
};

struct subcmd_regex101_t {
    using action_t = std::function<perform_result_t(const subcmd_regex101_t&)>;

    CLI::App* sr_app{nullptr};
    action_t sr_action;
    std::string sr_import_url;
    std::string sr_import_name;
    std::string sr_import_regex_name{"std"};

    subcmd_regex101_t& set_action(action_t act)
    {
        if (!this->sr_action) {
            this->sr_action = std::move(act);
        }
        return *this;
    }

    static perform_result_t default_action(const subcmd_regex101_t& sr)
    {
        auto um = console::user_message::error(
            "expecting an operation related to the regex101.com integration");
        um.with_help(
            sr.sr_app->get_subcommands({})
            | lnav::itertools::fold(
                subcmd_reducer, attr_line_t{"the available operations are:"}));

        return {um};
    }

    static perform_result_t list_action(const subcmd_regex101_t&)
    {
        auto get_res = lnav::session::regex101::get_entries();

        if (get_res.isErr()) {
            return {
                console::user_message::error(
                    "unable to read regex101 entries from DB")
                    .with_reason(get_res.unwrapErr()),
            };
        }

        auto entries = get_res.unwrap()
            | lnav::itertools::map([](const auto& elem) {
                           return fmt::format(
                               FMT_STRING("   format {} regex {} regex101\n"),
                               elem.re_format_name,
                               elem.re_regex_name);
                       })
            | lnav::itertools::fold(
                           [](const auto& elem, auto& accum) {
                               return accum.append(elem);
                           },
                           attr_line_t{});

        auto um = console::user_message::ok(
            entries.add_header("the following regex101 entries were found:\n")
                .with_default("no regex101 entries found"));

        return {um};
    }

    static perform_result_t import_action(const subcmd_regex101_t& sr)
    {
        auto import_res = regex101::import(
            sr.sr_import_url, sr.sr_import_name, sr.sr_import_regex_name);

        if (import_res.isOk()) {
            return {
                lnav::console::user_message::ok(
                    attr_line_t("converted regex101 entry to format file: ")
                        .append(lnav::roles::file(import_res.unwrap())))
                    .with_note("the converted format may still have errors")
                    .with_help(
                        attr_line_t(
                            "use the following command to patch the regex as "
                            "more changes are made on regex101.com:\n")
                            .appendf(FMT_STRING("   lnav -m format {} regex {} "
                                                "regex101 pull"),
                                     sr.sr_import_name,
                                     sr.sr_import_regex_name)),
            };
        }

        return {
            import_res.unwrapErr(),
        };
    }
};

using operations_v = mapbox::util::variant<no_subcmd_t,
                                           subcmd_config_t,
                                           subcmd_format_t,
                                           subcmd_piper_t,
                                           subcmd_regex101_t>;

class operations {
public:
    operations_v o_ops;
};

std::shared_ptr<operations>
describe_cli(CLI::App& app, int argc, char* argv[])
{
    auto retval = std::make_shared<operations>();

    retval->o_ops = no_subcmd_t{
        &app,
    };

    app.add_flag("-m", "Switch to the management CLI mode.");

    subcmd_config_t config_args;
    subcmd_format_t format_args;
    subcmd_piper_t piper_args;
    subcmd_regex101_t regex101_args;

    {
        auto* subcmd_config
            = app.add_subcommand("config",
                                 "perform operations on the lnav configuration")
                  ->callback([&]() {
                      config_args.set_action(subcmd_config_t::default_action);
                      retval->o_ops = config_args;
                  });
        config_args.sc_config_app = subcmd_config;

        subcmd_config->add_subcommand("get", "print the current configuration")
            ->callback(
                [&]() { config_args.set_action(subcmd_config_t::get_action); });

        subcmd_config
            ->add_subcommand("blame",
                             "print the configuration options and their source")
            ->callback([&]() {
                config_args.set_action(subcmd_config_t::blame_action);
            });

        auto* sub_file_options = subcmd_config->add_subcommand(
            "file-options", "print the options applied to specific files");

        sub_file_options->add_option(
            "path", config_args.sc_path, "the path to the file");
        sub_file_options->callback([&]() {
            config_args.set_action(subcmd_config_t::file_options_action);
        });
    }

    {
        auto* subcmd_format
            = app.add_subcommand("format",
                                 "perform operations on log file formats")
                  ->callback([&]() {
                      format_args.set_action(subcmd_format_t::default_action);
                      retval->o_ops = format_args;
                  });
        format_args.sf_format_app = subcmd_format;
        subcmd_format
            ->add_option(
                "format_name", format_args.sf_name, "the name of the format")
            ->expected(1);

        {
            subcmd_format
                ->add_subcommand("get", "print information about a format")
                ->callback([&]() {
                    format_args.set_action(subcmd_format_t::get_action);
                });
        }

        {
            subcmd_format
                ->add_subcommand("source",
                                 "print the path of the first source file "
                                 "containing this format")
                ->callback([&]() {
                    format_args.set_action(subcmd_format_t::source_action);
                });
        }

        {
            subcmd_format
                ->add_subcommand("sources",
                                 "print the paths of all source files "
                                 "containing this format")
                ->callback([&]() {
                    format_args.set_action(subcmd_format_t::sources_action);
                });
        }

        {
            auto* subcmd_format_regex
                = subcmd_format
                      ->add_subcommand(
                          "regex",
                          "operate on the format's regular expressions")
                      ->callback([&]() {
                          format_args.set_action(
                              subcmd_format_t::default_regex_action);
                      });
            format_args.sf_regex_app = subcmd_format_regex;
            subcmd_format_regex->add_option(
                "regex-name",
                format_args.sf_regex_name,
                "the name of the regular expression to operate on");

            {
                auto* subcmd_format_regex_regex101
                    = subcmd_format_regex
                          ->add_subcommand("regex101",
                                           "use regex101.com to edit this "
                                           "regular expression")
                          ->callback([&]() {
                              format_args.set_action(
                                  subcmd_format_t::regex101_default_action);
                          });
                format_args.sf_regex101_app = subcmd_format_regex_regex101;

                {
                    subcmd_format_regex_regex101
                        ->add_subcommand("push",
                                         "create/update an entry for "
                                         "this regex on regex101.com")
                        ->callback([&]() {
                            format_args.set_action(
                                subcmd_format_t::regex101_push_action);
                        });
                    subcmd_format_regex_regex101
                        ->add_subcommand(
                            "pull",
                            "create a patch format file for this "
                            "regular expression based on the entry in "
                            "regex101.com")
                        ->callback([&]() {
                            format_args.set_action(
                                subcmd_format_t::regex101_pull_action);
                        });
                    subcmd_format_regex_regex101
                        ->add_subcommand(
                            "delete",
                            "delete the entry regex101.com that was "
                            "created by a push operation")
                        ->callback([&]() {
                            format_args.set_action(
                                subcmd_format_t::regex101_delete_action);
                        });
                }
            }
        }
    }

    {
        auto* subcmd_piper
            = app.add_subcommand("piper", "perform operations on piper storage")
                  ->callback([&]() {
                      piper_args.set_action(subcmd_piper_t::default_action);
                      retval->o_ops = piper_args;
                  });
        piper_args.sp_app = subcmd_piper;

        subcmd_piper
            ->add_subcommand("list", "print the available piper captures")
            ->callback(
                [&]() { piper_args.set_action(subcmd_piper_t::list_action); });

        subcmd_piper->add_subcommand("clean", "remove all piper captures")
            ->callback(
                [&]() { piper_args.set_action(subcmd_piper_t::clean_action); });
    }

    {
        auto* subcmd_regex101
            = app.add_subcommand("regex101",
                                 "create and edit log message regular "
                                 "expressions using regex101.com")
                  ->callback([&]() {
                      regex101_args.set_action(
                          subcmd_regex101_t::default_action);
                      retval->o_ops = regex101_args;
                  });
        regex101_args.sr_app = subcmd_regex101;

        {
            subcmd_regex101
                ->add_subcommand("list",
                                 "list the log format regular expression "
                                 "linked to entries on regex101.com")
                ->callback([&]() {
                    regex101_args.set_action(subcmd_regex101_t::list_action);
                });
        }
        {
            auto* subcmd_regex101_import
                = subcmd_regex101
                      ->add_subcommand("import",
                                       "create a new format from a regular "
                                       "expression on regex101.com")
                      ->callback([&]() {
                          regex101_args.set_action(
                              subcmd_regex101_t::import_action);
                      });

            subcmd_regex101_import->add_option(
                "url",
                regex101_args.sr_import_url,
                "The regex101.com url to construct a log format from");
            subcmd_regex101_import->add_option("name",
                                               regex101_args.sr_import_name,
                                               "The name for the log format");
            subcmd_regex101_import
                ->add_option("regex-name",
                             regex101_args.sr_import_regex_name,
                             "The name for the new regex")
                ->always_capture_default();
        }
    }

    app.parse(argc, argv);

    return retval;
}

perform_result_t
perform(std::shared_ptr<operations> opts)
{
    return opts->o_ops.match(
        [](const no_subcmd_t& ns) -> perform_result_t {
            auto um = console::user_message::error(
                attr_line_t("expecting an operation to perform"));
            um.with_help(ns.ns_root_app->get_subcommands({})
                         | lnav::itertools::fold(
                             subcmd_reducer,
                             attr_line_t{"the available operations are:"}));

            return {um};
        },
        [](const subcmd_config_t& sc) { return sc.sc_action(sc); },
        [](const subcmd_format_t& sf) { return sf.sf_action(sf); },
        [](const subcmd_piper_t& sp) { return sp.sp_action(sp); },
        [](const subcmd_regex101_t& sr) { return sr.sr_action(sr); });
}

}  // namespace management
}  // namespace lnav
