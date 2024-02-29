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

#include "base/itertools.hh"
#include "base/result.h"
#include "base/string_util.hh"
#include "fmt/format.h"
#include "itertools.similar.hh"
#include "lnav.hh"
#include "lnav_config.hh"
#include "log_format.hh"
#include "log_format_ext.hh"
#include "mapbox/variant.hpp"
#include "regex101.import.hh"
#include "session_data.hh"

using namespace lnav::roles::literals;

namespace lnav {

namespace management {

struct no_subcmd_t {
    CLI::App* ns_root_app{nullptr};
};

inline attr_line_t&
symbol_reducer(const std::string& elem, attr_line_t& accum)
{
    return accum.append("\n   ").append(lnav::roles::symbol(elem));
}

inline attr_line_t&
subcmd_reducer(const CLI::App* app, attr_line_t& accum)
{
    return accum.append("\n \u2022 ")
        .append(lnav::roles::keyword(app->get_name()))
        .append(": ")
        .append(app->get_description());
}

struct subcmd_config_t {
    using action_t = std::function<perform_result_t(const subcmd_config_t&)>;

    CLI::App* sc_config_app{nullptr};
    action_t sc_action;

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
                    .add_header("the available formats are:"));

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
                    .add_header("did you mean one of the following?"));

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
                    symbol_reducer, attr_line_t{"the available regexes are:"}));

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
                .add_header("did you mean one of the following?"));

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

using operations_v = mapbox::util::
    variant<no_subcmd_t, subcmd_config_t, subcmd_format_t, subcmd_regex101_t>;

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
        [](const subcmd_regex101_t& sr) { return sr.sr_action(sr); });
}

}  // namespace management
}  // namespace lnav
