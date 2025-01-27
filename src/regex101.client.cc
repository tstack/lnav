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

#include <filesystem>

#include "regex101.client.hh"

#include <curl/curl.h>

#include "base/itertools.hh"
#include "config.h"
#include "curl_looper.hh"
#include "yajlpp/yajlpp_def.hh"

namespace regex101::client {

static const typed_json_path_container<entry>&
get_entry_handlers()
{
    static const json_path_handler_base::enum_value_t CRITERIA_ENUM[] = {
        {"DOES_MATCH", unit_test::criteria::DOES_MATCH},
        {"DOES_NOT_MATCH", unit_test::criteria::DOES_NOT_MATCH},

        json_path_handler_base::ENUM_TERMINATOR,
    };

    static const json_path_container UNIT_TEST_HANDLERS = {
        yajlpp::property_handler("description")
            .for_field(&unit_test::ut_description),
        yajlpp::property_handler("testString")
            .for_field(&unit_test::ut_test_string),
        yajlpp::property_handler("target").for_field(&unit_test::ut_target),
        yajlpp::property_handler("criteria")
            .with_enum_values(CRITERIA_ENUM)
            .for_field(&unit_test::ut_criteria),
    };

    static const typed_json_path_container<entry> retval = {
        yajlpp::property_handler("dateCreated")
            .for_field(&entry::e_date_created),
        yajlpp::property_handler("regex").for_field(&entry::e_regex),
        yajlpp::property_handler("testString").for_field(&entry::e_test_string),
        yajlpp::property_handler("flags").for_field(&entry::e_flags),
        yajlpp::property_handler("delimiter").for_field(&entry::e_delimiter),
        yajlpp::property_handler("flavor").for_field(&entry::e_flavor),
        yajlpp::property_handler("unitTests#")
            .for_field(&entry::e_unit_tests)
            .with_children(UNIT_TEST_HANDLERS),
        yajlpp::property_handler("permalinkFragment")
            .for_field(&entry::e_permalink_fragment),
    };

    return retval;
}

static const typed_json_path_container<upsert_response>&
get_response_handlers()
{
    static const typed_json_path_container<upsert_response> retval = {
        yajlpp::property_handler("deleteCode")
            .for_field(&upsert_response::cr_delete_code),
        yajlpp::property_handler("permalinkFragment")
            .for_field(&upsert_response::cr_permalink_fragment),
        yajlpp::property_handler("version").for_field(
            &upsert_response::cr_version),
    };

    return retval;
}

static const std::filesystem::path REGEX101_BASE_URL
    = "https://regex101.com/api/regex";
static const char* USER_AGENT = "lnav/" PACKAGE_VERSION;

Result<upsert_response, lnav::console::user_message>
upsert(entry& en)
{
    auto entry_json = get_entry_handlers().to_string(en);

    curl_request cr(REGEX101_BASE_URL.string());

    curl_easy_setopt(cr, CURLOPT_URL, cr.get_name().c_str());
    curl_easy_setopt(cr, CURLOPT_POST, 1);
    curl_easy_setopt(cr, CURLOPT_POSTFIELDS, entry_json.c_str());
    curl_easy_setopt(cr, CURLOPT_POSTFIELDSIZE, entry_json.size());
    curl_easy_setopt(cr, CURLOPT_USERAGENT, USER_AGENT);

    auto_mem<curl_slist> list(curl_slist_free_all);

    list = curl_slist_append(list, "Content-Type: application/json");

    curl_easy_setopt(cr, CURLOPT_HTTPHEADER, list.in());

    auto perform_res = cr.perform();
    if (perform_res.isErr()) {
        return Err(
            lnav::console::user_message::error(
                "unable to create entry on regex101.com")
                .with_reason(curl_easy_strerror(perform_res.unwrapErr())));
    }

    auto response = perform_res.unwrap();
    auto resp_code = cr.get_response_code();
    if (resp_code != 200) {
        return Err(lnav::console::user_message::error(
                       "unable to create entry on regex101.com")
                       .with_reason(attr_line_t()
                                        .append("received response code ")
                                        .append(lnav::roles::number(
                                            fmt::to_string(resp_code)))
                                        .append(" content ")
                                        .append_quoted(response)));
    }

    auto parse_res = get_response_handlers()
                         .parser_for(intern_string::lookup(cr.get_name()))
                         .with_ignore_unused(true)
                         .of(response);
    if (parse_res.isOk()) {
        return Ok(parse_res.unwrap());
    }

    auto errors = parse_res.unwrapErr();
    return Err(lnav::console::user_message::error(
                   "unable to create entry on regex101.com")
                   .with_reason(errors[0].to_attr_line({})));
}

struct retrieve_entity {
    std::string re_permalink_fragment;
    std::vector<int32_t> re_versions;
};

static const typed_json_path_container<retrieve_entity> RETRIEVE_ENTITY_HANDLERS
    = {
        yajlpp::property_handler("permalinkFragment")
            .for_field(&retrieve_entity::re_permalink_fragment),
        yajlpp::property_handler("versions#")
            .for_field(&retrieve_entity::re_versions),
};

retrieve_result_t
retrieve(const std::string& permalink)
{
    auto entry_url = REGEX101_BASE_URL / permalink;
    curl_request entry_req(entry_url.string());

    curl_easy_setopt(entry_req, CURLOPT_URL, entry_req.get_name().c_str());
    curl_easy_setopt(entry_req, CURLOPT_USERAGENT, USER_AGENT);

    auto perform_res = entry_req.perform();
    if (perform_res.isErr()) {
        return lnav::console::user_message::error(
                   attr_line_t("unable to get entry ")
                       .append_quoted(lnav::roles::symbol(permalink))
                       .append(" on regex101.com"))
            .with_reason(curl_easy_strerror(perform_res.unwrapErr()));
    }

    auto response = perform_res.unwrap();
    auto resp_code = entry_req.get_response_code();
    if (resp_code == 404) {
        return no_entry{};
    }
    if (resp_code != 200) {
        return lnav::console::user_message::error(
                   attr_line_t("unable to get entry ")
                       .append_quoted(lnav::roles::symbol(permalink))
                       .append(" on regex101.com"))
            .with_reason(
                attr_line_t()
                    .append("received response code ")
                    .append(lnav::roles::number(fmt::to_string(resp_code)))
                    .append(" content ")
                    .append_quoted(response));
    }

    auto parse_res
        = RETRIEVE_ENTITY_HANDLERS
              .parser_for(intern_string::lookup(entry_req.get_name()))
              .with_ignore_unused(true)
              .of(response);

    if (parse_res.isErr()) {
        auto parse_errors = parse_res.unwrapErr();

        return lnav::console::user_message::error(
                   attr_line_t("unable to get entry ")
                       .append_quoted(lnav::roles::symbol(permalink))
                       .append(" on regex101.com"))
            .with_reason(parse_errors[0].to_attr_line({}));
    }

    auto entry_value = parse_res.unwrap();

    auto latest_version = entry_value.re_versions | lnav::itertools::max();
    if (!latest_version) {
        return no_entry{};
    }

    auto version_url = entry_url / fmt::to_string(latest_version.value());
    curl_request version_req(version_url.string());

    curl_easy_setopt(version_req, CURLOPT_URL, version_req.get_name().c_str());
    curl_easy_setopt(version_req, CURLOPT_USERAGENT, USER_AGENT);

    auto version_perform_res = version_req.perform();
    if (version_perform_res.isErr()) {
        return lnav::console::user_message::error(
                   attr_line_t("unable to get entry version ")
                       .append_quoted(lnav::roles::symbol(version_url.string()))
                       .append(" on regex101.com"))
            .with_reason(curl_easy_strerror(version_perform_res.unwrapErr()));
    }

    auto version_response = version_perform_res.unwrap();
    auto version_parse_res
        = get_entry_handlers()
              .parser_for(intern_string::lookup(version_req.get_name()))
              .with_ignore_unused(true)
              .of(version_response);

    if (version_parse_res.isErr()) {
        auto parse_errors = version_parse_res.unwrapErr();
        return lnav::console::user_message::error(
                   attr_line_t("unable to get entry version ")
                       .append_quoted(lnav::roles::symbol(version_url.string()))
                       .append(" on regex101.com"))
            .with_reason(parse_errors[0].to_attr_line({}));
    }

    auto retval = version_parse_res.unwrap();

    retval.e_permalink_fragment = permalink;

    return retval;
}

struct delete_entity {
    std::string de_delete_code;
};

static const typed_json_path_container<delete_entity> DELETE_ENTITY_HANDLERS = {
    yajlpp::property_handler("deleteCode")
        .for_field(&delete_entity::de_delete_code),
};

Result<void, lnav::console::user_message>
delete_entry(const std::string& delete_code)
{
    curl_request cr(REGEX101_BASE_URL.string());
    delete_entity entity{delete_code};
    auto entity_json = DELETE_ENTITY_HANDLERS.to_string(entity);

    curl_easy_setopt(cr, CURLOPT_URL, cr.get_name().c_str());
    curl_easy_setopt(cr, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(cr, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(cr, CURLOPT_POSTFIELDS, entity_json.c_str());
    curl_easy_setopt(cr, CURLOPT_POSTFIELDSIZE, entity_json.size());

    auto_mem<curl_slist> list(curl_slist_free_all);

    list = curl_slist_append(list, "Content-Type: application/json");

    curl_easy_setopt(cr, CURLOPT_HTTPHEADER, list.in());

    auto perform_res = cr.perform();
    if (perform_res.isErr()) {
        return Err(
            lnav::console::user_message::error(
                "unable to delete entry on regex101.com")
                .with_reason(curl_easy_strerror(perform_res.unwrapErr())));
    }

    auto response = perform_res.unwrap();
    auto resp_code = cr.get_response_code();
    if (resp_code != 200) {
        return Err(lnav::console::user_message::error(
                       "unable to delete entry on regex101.com")
                       .with_reason(attr_line_t()
                                        .append("received response code ")
                                        .append(lnav::roles::number(
                                            fmt::to_string(resp_code)))
                                        .append(" content ")
                                        .append_quoted(response)));
    }

    return Ok();
}

std::string
to_edit_url(const std::string& permalink)
{
    return fmt::format(FMT_STRING("https://regex101.com/r/{}"), permalink);
}

bool
unit_test::operator==(const unit_test& rhs) const
{
    return ut_description == rhs.ut_description
        && ut_test_string == rhs.ut_test_string && ut_target == rhs.ut_target
        && ut_criteria == rhs.ut_criteria;
}

bool
unit_test::operator!=(const unit_test& rhs) const
{
    return !(rhs == *this);
}

bool
entry::operator==(const entry& rhs) const
{
    return e_regex == rhs.e_regex && e_test_string == rhs.e_test_string
        && e_flavor == rhs.e_flavor && e_unit_tests == rhs.e_unit_tests;
}

bool
entry::operator!=(const entry& rhs) const
{
    return !(rhs == *this);
}

}  // namespace regex101::client