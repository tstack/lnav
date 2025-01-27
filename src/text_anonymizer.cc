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

#include "text_anonymizer.hh"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <netinet/in.h>

#include "animals-json.h"
#include "config.h"
#include "data_scanner.hh"
#include "diseases-json.h"
#include "hasher.hh"
#include "pcrepp/pcre2pp.hh"
#include "words-json.h"
#include "yajlpp/yajlpp_def.hh"

namespace lnav {

struct random_list {
    std::vector<std::string> rl_data;

    std::string at_index(size_t index) const
    {
        auto counter = index / this->rl_data.size();
        auto mod = index % this->rl_data.size();

        auto retval = this->rl_data[mod];
        if (counter > 0) {
            retval = fmt::format(FMT_STRING("{}{}"), retval, counter);
        }
        return retval;
    }
};

static const typed_json_path_container<random_list>&
get_random_list_handlers()
{
    static const typed_json_path_container<random_list> retval = {
        yajlpp::property_handler("data#").for_field(&random_list::rl_data),
    };

    return retval;
}

static random_list
load_word_list()
{
    static const intern_string_t name
        = intern_string::lookup(words_json.get_name());
    auto sfp = words_json.to_string_fragment_producer();
    auto parse_res = get_random_list_handlers()
                         .parser_for(name)
                         .with_ignore_unused(false)
                         .of(*sfp);

    return parse_res.unwrap();
}

static const random_list&
get_word_list()
{
    static const auto retval = load_word_list();

    return retval;
}

static random_list
load_animal_list()
{
    static const intern_string_t name
        = intern_string::lookup(animals_json.get_name());
    auto sfp = animals_json.to_string_fragment_producer();
    auto parse_res = get_random_list_handlers()
                         .parser_for(name)
                         .with_ignore_unused(false)
                         .of(*sfp);

    return parse_res.unwrap();
}

static const random_list&
get_animal_list()
{
    static const auto retval = load_animal_list();

    return retval;
}

static random_list
load_disease_list()
{
    static const intern_string_t name
        = intern_string::lookup(diseases_json.get_name());
    auto sfp = diseases_json.to_string_fragment_producer();
    auto parse_res = get_random_list_handlers()
                         .parser_for(name)
                         .with_ignore_unused(false)
                         .of(*sfp);

    return parse_res.unwrap();
}

static const random_list&
get_disease_list()
{
    static const auto retval = load_disease_list();

    return retval;
}

std::string
text_anonymizer::next(string_fragment line)
{
    data_scanner ds(line);
    std::string retval;

    while (true) {
        auto tok_res = ds.tokenize2();
        if (!tok_res) {
            break;
        }

        switch (tok_res->tr_token) {
            case DT_URL: {
                auto url_str = tok_res->to_string();
                auto_mem<CURLU> cu(curl_url_cleanup);
                cu = curl_url();

                if (curl_url_set(cu, CURLUPART_URL, url_str.c_str(), 0)
                    != CURLUE_OK)
                {
                    retval += "<unparseable-url>";
                } else {
                    auto_mem<char> url_part(curl_free);

                    if (curl_url_get(
                            cu, CURLUPART_USER, url_part.out(), CURLU_URLDECODE)
                        == CURLUE_OK)
                    {
                        auto anon_user = this->get_default(
                            this->ta_user_names,
                            url_part.in(),
                            [](size_t size, auto& user) {
                                return get_animal_list().at_index(size);
                            });
                        curl_url_set(cu,
                                     CURLUPART_USER,
                                     anon_user.c_str(),
                                     CURLU_URLENCODE);
                    }

                    if (curl_url_get(cu,
                                     CURLUPART_PASSWORD,
                                     url_part.out(),
                                     CURLU_URLDECODE)
                        == CURLUE_OK)
                    {
                        auto anon_pass
                            = hasher()
                                  .update(url_part.in(), strlen(url_part.in()))
                                  .to_string();
                        curl_url_set(cu,
                                     CURLUPART_PASSWORD,
                                     anon_pass.c_str(),
                                     CURLU_URLENCODE);
                    }

                    if (curl_url_get(
                            cu, CURLUPART_HOST, url_part.out(), CURLU_URLDECODE)
                        == CURLUE_OK)
                    {
                        auto anon_host = this->get_default(
                            this->ta_host_names,
                            url_part.in(),
                            [](size_t size, auto& hn) {
                                const auto& diseases = get_disease_list();

                                return fmt::format(FMT_STRING("{}.example.com"),
                                                   diseases.at_index(size));
                            });
                        curl_url_set(cu,
                                     CURLUPART_HOST,
                                     anon_host.c_str(),
                                     CURLU_URLENCODE);
                    }

                    if (curl_url_get(
                            cu, CURLUPART_PATH, url_part.out(), CURLU_URLDECODE)
                        == CURLUE_OK)
                    {
                        std::filesystem::path url_path(url_part.in());
                        std::filesystem::path anon_path;

                        for (const auto& comp : url_path) {
                            if (comp == comp.root_path()) {
                                anon_path = anon_path / comp;
                                continue;
                            }
                            anon_path = anon_path / this->next(comp.string());
                        }
                        curl_url_set(cu,
                                     CURLUPART_PATH,
                                     anon_path.c_str(),
                                     CURLU_URLENCODE);
                    }

                    if (curl_url_get(cu,
                                     CURLUPART_QUERY,
                                     url_part.out(),
                                     CURLU_URLDECODE)
                        == CURLUE_OK)
                    {
                        static const auto SPLIT_RE
                            = lnav::pcre2pp::code::from_const(R"((&))");

                        curl_url_set(cu, CURLUPART_QUERY, nullptr, 0);

                        auto url_query
                            = string_fragment::from_c_str(url_part.in());
                        auto replacer = [this, &cu](const std::string& comp) {
                            std::string anon_query;

                            auto eq_index = comp.find('=');
                            if (eq_index != std::string::npos) {
                                auto new_key
                                    = this->next(comp.substr(0, eq_index));
                                auto new_value
                                    = this->next(comp.substr(eq_index + 1));
                                anon_query = fmt::format(
                                    FMT_STRING("{}={}"), new_key, new_value);
                            } else {
                                anon_query = this->next(comp);
                            }

                            curl_url_set(cu,
                                         CURLUPART_QUERY,
                                         anon_query.c_str(),
                                         CURLU_URLENCODE | CURLU_APPENDQUERY);
                        };

                        auto loop_res
                            = SPLIT_RE.capture_from(url_query).for_each(
                                [&replacer](lnav::pcre2pp::match_data& md) {
                                    replacer(md.leading().to_string());
                                });
                        if (loop_res.isOk()) {
                            replacer(loop_res.unwrap().to_string());
                        }
                    }

                    if (curl_url_get(cu,
                                     CURLUPART_FRAGMENT,
                                     url_part.out(),
                                     CURLU_URLDECODE)
                        == CURLUE_OK)
                    {
                        auto anon_frag = this->next(
                            string_fragment::from_c_str(url_part.in()));

                        curl_url_set(cu,
                                     CURLUPART_FRAGMENT,
                                     anon_frag.c_str(),
                                     CURLU_URLENCODE);
                    }

                    auto_mem<char> anon_url(curl_free);
                    if (curl_url_get(cu, CURLUPART_URL, anon_url.out(), 0)
                        == CURLUE_OK)
                    {
                        retval.append(anon_url.in());
                    }
                }
                break;
            }
            case DT_PATH: {
                std::filesystem::path inp_path(tok_res->to_string());
                std::filesystem::path anon_path;

                for (const auto& comp : inp_path) {
                    auto comp_str = comp.string();
                    if (comp == comp.root_path() || comp == inp_path) {
                        anon_path = anon_path / comp;
                        continue;
                    }
                    anon_path = anon_path / this->next(comp_str);
                }

                retval += anon_path.string();
                break;
            }
            case DT_CREDIT_CARD_NUMBER: {
                auto cc = tok_res->to_string();
                auto has_spaces = cc.size() > 16;
                auto new_end = std::remove_if(
                    cc.begin(), cc.end(), [](auto ch) { return ch == ' '; });
                cc.erase(new_end, cc.end());
                auto anon_cc = hasher().update(cc).to_string().substr(0, 16);

                if (has_spaces) {
                    anon_cc.insert(12, " ");
                    anon_cc.insert(8, " ");
                    anon_cc.insert(4, " ");
                }

                retval += anon_cc;
                break;
            }
            case DT_MAC_ADDRESS: {
                // 00-00-5E-00-53-00
                auto mac_addr = tok_res->to_string();

                retval += this->get_default(
                    this->ta_mac_addresses,
                    mac_addr,
                    [](size_t size, auto& inp) {
                        uint32_t base_mac = 0x5e005300;

                        base_mac += size;
                        auto anon_mac = byte_array<6>::from({
                            0x00,
                            0x00,
                            (unsigned char) ((base_mac >> 24) & 0xff),
                            (unsigned char) ((base_mac >> 16) & 0xff),
                            (unsigned char) ((base_mac >> 8) & 0xff),
                            (unsigned char) ((base_mac >> 0) & 0xff),
                        });

                        return anon_mac.to_string(std::make_optional(inp[2]));
                    });
                break;
            }
            case DT_HEX_DUMP: {
                auto hex_str = tok_res->to_string();
                auto hash_str = hasher().update(hex_str).to_array().to_string(
                    std::make_optional(hex_str[2]));
                std::string anon_hex;

                while (anon_hex.size() < hex_str.size()) {
                    anon_hex += hash_str;
                }
                anon_hex.resize(hex_str.size());

                retval += anon_hex;
                break;
            }
            case DT_IPV4_ADDRESS: {
                auto ipv4 = tok_res->to_string();
                retval += this->get_default(
                    this->ta_ipv4_addresses, ipv4, [](size_t size, auto& _) {
                        char anon_ipv4[INET_ADDRSTRLEN];
                        struct in_addr ia;

                        inet_aton("10.0.0.0", &ia);
                        ia.s_addr = htonl(ntohl(ia.s_addr) + 1 + size);
                        inet_ntop(AF_INET, &ia, anon_ipv4, sizeof(anon_ipv4));
                        return std::string{anon_ipv4};
                    });
                break;
            }
            case DT_IPV6_ADDRESS: {
                auto ipv6 = tok_res->to_string();
                retval += this->get_default(
                    this->ta_ipv6_addresses, ipv6, [](size_t size, auto& _) {
                        char anon_ipv6[INET6_ADDRSTRLEN];
                        struct in6_addr ia;
                        uint32_t* ia6_addr32 = (uint32_t*) &ia.s6_addr[12];

                        inet_pton(AF_INET6, "2001:db8::", &ia);
                        *ia6_addr32 = htonl(ntohl(*ia6_addr32) + 1 + size);
                        inet_ntop(AF_INET6, &ia, anon_ipv6, sizeof(anon_ipv6));
                        return std::string{anon_ipv6};
                    });
                break;
            }
            case DT_EMAIL: {
                auto email_addr = tok_res->to_string();
                auto at_index = email_addr.find('@');

                retval += fmt::format(
                    FMT_STRING("{}@{}.example.com"),
                    this->get_default(this->ta_user_names,
                                      email_addr.substr(0, at_index),
                                      [](auto size, const auto& inp) {
                                          return get_animal_list().at_index(
                                              size);
                                      }),
                    this->get_default(this->ta_host_names,
                                      email_addr.substr(at_index + 1),
                                      [](auto size, const auto& inp) {
                                          return get_disease_list().at_index(
                                              size);
                                      }));
                break;
            }
            case DT_WORD:
            case DT_SYMBOL: {
                static const auto SPLIT_RE = lnav::pcre2pp::code::from_const(
                    R"((\.|::|_|-|/|\\|\d+))");
                auto symbol_frag = ds.to_string_fragment(tok_res->tr_capture);
                auto sym_provider = [](auto size, const auto& inp) {
                    if (inp.size() <= 4) {
                        return inp;
                    }

                    auto comp_frag = string_fragment::from_str(inp);
                    return string_fragment::from_str(
                               get_word_list().at_index(size))
                        .to_string_with_case_style(
                            comp_frag.detect_text_case_style());
                };

                auto cap_res
                    = SPLIT_RE.capture_from(symbol_frag)
                          .for_each([this, &retval, &sym_provider](
                                        lnav::pcre2pp::match_data& md) {
                              auto comp = md.leading().to_string();
                              retval
                                  += this->get_default(
                                         this->ta_symbols, comp, sym_provider)
                                  + md[0]->to_string();
                          });
                if (cap_res.isErr()) {
                    retval += "<symbol>";
                } else {
                    auto remaining = cap_res.unwrap().to_string();

                    retval += this->get_default(
                        this->ta_symbols, remaining, sym_provider);
                }
                break;
            }
            case DT_QUOTED_STRING: {
                auto anon_inner = this->next(
                    ds.to_string_fragment(tok_res->tr_inner_capture)
                        .to_string());

                retval += line.sub_range(tok_res->tr_capture.c_begin,
                                         tok_res->tr_inner_capture.c_begin)
                              .to_string()
                    + anon_inner
                    + ds.to_string_fragment(tok_res->tr_capture).back();
                break;
            }
            case DT_XML_OPEN_TAG: {
                auto open_tag = tok_res->to_string();
                auto space_index = open_tag.find(' ');

                if (space_index == std::string::npos) {
                    retval += open_tag;
                } else {
                    static const auto ATTR_RE
                        = lnav::pcre2pp::code::from_const(R"([\w\-]+=)");
                    thread_local auto md
                        = lnav::pcre2pp::match_data::unitialized();

                    auto remaining = string_fragment::from_str_range(
                        open_tag, space_index, open_tag.size());

                    retval += open_tag.substr(0, space_index + 1);
                    while (!remaining.empty()) {
                        auto cap_res = ATTR_RE.capture_from(remaining)
                                           .into(md)
                                           .matches()
                                           .ignore_error();

                        if (!cap_res) {
                            break;
                        }

                        retval += md.leading();
                        retval += md[0]->to_string();
                        remaining = md.remaining();
                        data_scanner ds(remaining);
                        auto attr_tok_res = ds.tokenize2();
                        if (!attr_tok_res) {
                            continue;
                        }
                        retval += this->next(attr_tok_res->to_string());
                        remaining = remaining.substr(
                            attr_tok_res->tr_capture.length());
                    }

                    retval += remaining.to_string();
                }
                break;
            }
            case DT_UUID: {
                retval
                    += hasher().update(tok_res->to_string()).to_uuid_string();
                break;
            }
            default: {
                log_debug("tok_re %d %d:%d",
                          tok_res->tr_token,
                          tok_res->tr_capture.c_begin,
                          tok_res->tr_capture.c_end);
                retval += tok_res->to_string();
                break;
            }
        }
    }

    return retval;
}

}  // namespace lnav
