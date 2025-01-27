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

#include "regex101.import.hh"

#include "base/fs_util.hh"
#include "base/itertools.hh"
#include "base/paths.hh"
#include "lnav_config.hh"
#include "log_format.hh"
#include "log_format_ext.hh"
#include "pcrepp/pcre2pp.hh"
#include "regex101.client.hh"
#include "session_data.hh"
#include "yajlpp/yajlpp.hh"

using namespace lnav::roles::literals;

static const std::set<std::string> SUPPORTED_FLAVORS = {
    "pcre",
    "pcre2",
};

Result<std::filesystem::path, lnav::console::user_message>
regex101::import(const std::string& url,
                 const std::string& name,
                 const std::string& pat_name)
{
    static const auto USER_URL = lnav::pcre2pp::code::from_const(
        R"(^https://regex101.com/r/(\w+)(?:/(\d+))?)");
    thread_local auto md = lnav::pcre2pp::match_data::unitialized();
    static const auto NAME_RE = lnav::pcre2pp::code::from_const(R"(^\w+$)");

    if (url.empty()) {
        return Err(lnav::console::user_message::error(
            "expecting a regex101.com URL to import"));
    }
    if (name.empty()) {
        return Err(lnav::console::user_message::error(
            "expecting a name for the new format"));
    }

    auto lformat = log_format::find_root_format(name.c_str());
    bool existing_format = false;

    if (lformat != nullptr) {
        auto* ext_format = dynamic_cast<external_log_format*>(lformat.get());

        if (ext_format) {
            auto found = ext_format->elf_pattern_order
                | lnav::itertools::find_if([&pat_name](const auto& elem) {
                             return elem->p_name == pat_name;
                         });
            if (!found) {
                existing_format = true;
            }
        }
    }

    auto name_find_res = NAME_RE.find_in(name).ignore_error();
    if (!name_find_res) {
        auto partial_len = NAME_RE.match_partial(name);
        return Err(
            lnav::console::user_message::error(
                attr_line_t("unable to import: ")
                    .append(lnav::roles::file(url)))
                .with_reason(attr_line_t("expecting a format name that matches "
                                         "the regular expression ")
                                 .append_quoted(NAME_RE.get_pattern()))
                .with_note(attr_line_t("   ")
                               .append_quoted(name)
                               .append("\n    ")
                               .append(partial_len, ' ')
                               .append("^ matched up to here"_comment)));
    }

    auto user_find_res
        = USER_URL.capture_from(url).into(md).matches().ignore_error();
    if (!user_find_res) {
        auto partial_len = USER_URL.match_partial(url);
        return Err(lnav::console::user_message::error(
                       attr_line_t("unrecognized regex101.com URL: ")
                           .append(lnav::roles::file(url)))
                       .with_reason(attr_line_t("expecting a URL that matches ")
                                        .append_quoted(USER_URL.get_pattern()))
                       .with_note(attr_line_t("   ")
                                      .append_quoted(url)
                                      .append("\n    ")
                                      .append(partial_len, ' ')
                                      .append("^ matched up to here"_comment)));
    }

    auto permalink = md[1]->to_string();

    auto format_filename = existing_format
        ? fmt::format(FMT_STRING("{}.regex101-{}.json"), name, permalink)
        : fmt::format(FMT_STRING("{}.json"), name);
    auto format_path
        = lnav::paths::dotlnav() / "formats" / "installed" / format_filename;

    if (std::filesystem::exists(format_path)) {
        return Err(lnav::console::user_message::error(
                       attr_line_t("unable to import: ")
                           .append(lnav::roles::file(url)))
                       .with_reason(
                           attr_line_t("format file already exists: ")
                               .append(lnav::roles::file(format_path.string())))
                       .with_help("delete the existing file to continue"));
    }

    auto retrieve_res = regex101::client::retrieve(permalink);
    if (retrieve_res.is<lnav::console::user_message>()) {
        return Err(retrieve_res.get<lnav::console::user_message>());
    }

    if (retrieve_res.is<regex101::client::no_entry>()) {
        return Err(lnav::console::user_message::error(
            attr_line_t("unknown regex101.com entry: ")
                .append(lnav::roles::symbol(url))));
    }

    auto entry = retrieve_res.get<regex101::client::entry>();

    if (SUPPORTED_FLAVORS.count(entry.e_flavor) == 0) {
        return Err(lnav::console::user_message::error(
                       attr_line_t("invalid regex ")
                           .append_quoted(lnav::roles::symbol(entry.e_regex))
                           .append(" from ")
                           .append_quoted(lnav::roles::symbol(url)))
                       .with_reason(attr_line_t("unsupported regex flavor: ")
                                        .append_quoted(
                                            lnav::roles::symbol(entry.e_flags)))
                       .with_help(attr_line_t("the supported flavors are: ")
                                      .join(SUPPORTED_FLAVORS,
                                            VC_ROLE.value(role_t::VCR_SYMBOL),
                                            ", ")));
    }

    auto regex_res = lnav::pcre2pp::code::from(entry.e_regex);
    if (regex_res.isErr()) {
        auto parse_error = regex_res.unwrapErr();
        return Err(lnav::console::user_message::error(
                       attr_line_t("invalid regex ")
                           .append_quoted(lnav::roles::symbol(entry.e_regex))
                           .append(" from ")
                           .append_quoted(lnav::roles::symbol(url)))
                       .with_reason(parse_error.get_message())
                       .with_help("fix the regex and try the import again"));
    }

    auto regex = regex_res.unwrap();
    yajlpp_gen gen;

    yajl_gen_config(gen, yajl_gen_beautify, true);
    {
        yajlpp_map root_map(gen);

        root_map.gen("$schema");
        root_map.gen(DEFAULT_FORMAT_SCHEMA);

        root_map.gen(name);
        {
            yajlpp_map format_map(gen);

            if (!existing_format) {
                format_map.gen("description");
                format_map.gen(fmt::format(
                    FMT_STRING(
                        "Format file generated from regex101 entry -- {}"),
                    url));
            }
            format_map.gen("regex");
            {
                yajlpp_map regex_map(gen);

                regex_map.gen(pat_name);
                {
                    yajlpp_map std_map(gen);

                    std_map.gen("pattern");
                    std_map.gen(entry.e_regex);
                }
            }
            if (!existing_format) {
                format_map.gen("value");
                {
                    yajlpp_map value_map(gen);

                    for (auto named_cap : regex.get_named_captures()) {
                        if (named_cap.get_name() == "body") {
                            // don't need to add this as a value
                            continue;
                        }

                        value_map.gen(named_cap.get_name());
                        {
                            yajlpp_map cap_map(gen);

                            cap_map.gen("kind");
                            cap_map.gen("string");
                        }
                    }
                }
            }
            format_map.gen("sample");
            {
                yajlpp_array sample_array(gen);

                if (!entry.e_test_string.empty()) {
                    yajlpp_map elem_map(gen);

                    elem_map.gen("line");
                    elem_map.gen(rtrim(entry.e_test_string));
                }
                for (const auto& ut : entry.e_unit_tests) {
                    if (ut.ut_test_string.empty()) {
                        continue;
                    }

                    yajlpp_map elem_map(gen);

                    if (!ut.ut_description.empty()) {
                        elem_map.gen("description");
                        elem_map.gen(ut.ut_description);
                    }
                    elem_map.gen("line");
                    elem_map.gen(rtrim(ut.ut_test_string));
                }
            }
        }
    }

    auto format_json = gen.to_string_fragment();
    auto write_res = lnav::filesystem::write_file(format_path, format_json);
    if (write_res.isErr()) {
        return Err(lnav::console::user_message::error(
                       attr_line_t("unable to create format file: ")
                           .append(lnav::roles::file(format_path)))
                       .with_reason(write_res.unwrapErr()));
    }

    lnav::session::regex101::insert_entry({name, pat_name, permalink, ""});

    return Ok(format_path);
}

std::filesystem::path
regex101::patch_path(const external_log_format* format,
                     const std::string& permalink)
{
    if (format->elf_format_source_order.empty()) {
        return lnav::paths::dotlnav() / "formats" / "installed"
            / fmt::format(FMT_STRING("{}.regex101-{}.json"),
                          format->get_name(),
                          permalink);
    }

    auto first_path = format->elf_format_source_order.front();

    return first_path.replace_extension(
        fmt::format(FMT_STRING("regex101-{}.json"), permalink));
}

Result<std::filesystem::path, lnav::console::user_message>
regex101::patch(const external_log_format* format,
                const std::string& pat_name,
                const regex101::client::entry& entry)
{
    yajlpp_gen gen;

    yajl_gen_config(gen, yajl_gen_beautify, true);
    {
        yajlpp_map root_map(gen);

        root_map.gen("$schema");
        root_map.gen(DEFAULT_FORMAT_SCHEMA);

        root_map.gen(format->get_name());
        {
            yajlpp_map format_map(gen);

            format_map.gen("regex");
            {
                yajlpp_map regex_map(gen);

                regex_map.gen(pat_name);
                {
                    yajlpp_map pat_map(gen);

                    pat_map.gen("pattern");
                    pat_map.gen(entry.e_regex);
                }
            }

            auto new_samples
                = entry.e_unit_tests
                | lnav::itertools::prepend(regex101::client::unit_test{
                    "",
                    entry.e_test_string,
                })
                | lnav::itertools::filter_out([&format](const auto& ut) {
                      if (ut.ut_test_string.empty()) {
                          return true;
                      }
                      return (format->elf_samples
                              | lnav::itertools::find_if(
                                  [&ut](const auto& samp) {
                                      return samp.s_line.pp_value
                                          == rtrim(ut.ut_test_string);
                                  }))
                          .has_value();
                  });

            if (!new_samples.empty()) {
                format_map.gen("sample");
                {
                    yajlpp_array sample_array(gen);

                    for (const auto& ut : entry.e_unit_tests) {
                        yajlpp_map elem_map(gen);

                        if (!ut.ut_description.empty()) {
                            elem_map.gen("description");
                            elem_map.gen(ut.ut_description);
                        }
                        elem_map.gen("line");
                        elem_map.gen(rtrim(ut.ut_test_string));
                    }
                }
            }
        }
    }

    auto retval
        = regex101::patch_path(format, entry.e_permalink_fragment.value());
    auto write_res
        = lnav::filesystem::write_file(retval, gen.to_string_fragment());
    if (write_res.isErr()) {
        return Err(lnav::console::user_message::error(
                       attr_line_t("unable to write format patch file: ")
                           .append(lnav::roles::file(retval.string())))
                       .with_reason(write_res.unwrapErr()));
    }

    return Ok(retval);
}

regex101::client::entry
regex101::convert_format_pattern(
    const external_log_format* format,
    std::shared_ptr<external_log_format::pattern> pattern)
{
    regex101::client::entry en;

    en.e_regex = pattern->p_pcre.pp_value->get_pattern();
    for (const auto& sample : format->elf_samples) {
        if (en.e_test_string.empty()) {
            en.e_test_string = sample.s_line.pp_value;
        } else {
            regex101::client::unit_test ut;

            ut.ut_test_string = sample.s_line.pp_value;
            ut.ut_description = sample.s_description;
            en.e_unit_tests.emplace_back(ut);
        }
    }

    return en;
}
