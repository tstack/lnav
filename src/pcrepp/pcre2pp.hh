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

#ifndef lnav_pcre2pp_hh
#define lnav_pcre2pp_hh

#define PCRE2_CODE_UNIT_WIDTH 8

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <pcre2.h>

#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "base/result.h"
#include "mapbox/variant.hpp"

namespace lnav::pcre2pp {

class code;
struct capture_builder;
class matcher;

struct input {
    string_fragment i_string;
    int i_offset{0};
    int i_next_offset{0};
};

class match_data {
public:
    static match_data unitialized() { return match_data{}; }

    string_fragment leading() const
    {
        return this->md_input.i_string.sub_range(this->md_input.i_offset,
                                                 this->md_ovector[0]);
    }

    string_fragment remaining() const
    {
        if (this->md_capture_end == 0 || this->md_input.i_next_offset == -1) {
            return string_fragment::invalid();
        }

        return string_fragment::from_byte_range(
            this->md_input.i_string.sf_string,
            this->md_input.i_string.sf_begin + this->md_input.i_next_offset,
            this->md_input.i_string.sf_end);
    }

    size_t capture_size(size_t index) const
    {
        const auto start = this->md_ovector[(index * 2)];
        const auto stop = this->md_ovector[(index * 2) + 1];

        return stop - start;
    }

    std::optional<string_fragment> operator[](size_t index) const
    {
        if (index >= this->md_capture_end) {
            return std::nullopt;
        }

        auto start = this->md_ovector[(index * 2)];
        auto stop = this->md_ovector[(index * 2) + 1];
        if (start == PCRE2_UNSET || stop == PCRE2_UNSET) {
            return std::nullopt;
        }

        return this->md_input.i_string.sub_range(start, stop);
    }

    template<typename T, std::size_t N>
    std::optional<string_fragment> operator[](const T (&name)[N]) const;

    size_t get_count() const { return this->md_capture_end; }

    uint32_t get_capacity() const { return this->md_ovector_count; }

    string_fragment get_mark() const
    {
        return string_fragment::from_c_str(pcre2_get_mark(this->md_data.in()));
    }

    std::string to_string() const;

private:
    friend matcher;
    friend code;

    match_data() = default;

    explicit match_data(auto_mem<pcre2_match_data> dat)
        : md_data(std::move(dat)),
          md_ovector(pcre2_get_ovector_pointer(this->md_data.in())),
          md_ovector_count(pcre2_get_ovector_count(this->md_data.in()))
    {
    }

    auto_mem<pcre2_match_data> md_data;
    const code* md_code{nullptr};
    input md_input;
    PCRE2_SIZE* md_ovector{nullptr};
    uint32_t md_ovector_count{0};
    size_t md_capture_end{0};
};

class matcher {
public:
    struct found {
        string_fragment f_all;
        string_fragment f_remaining;
    };
    struct not_found {};
    struct error {
        const code* e_code{nullptr};
        int e_error_code{0};
        std::string get_message();
    };

    class matches_result
        : public mapbox::util::variant<found, not_found, error> {
    public:
        using variant::variant;

        std::optional<found> ignore_error()
        {
            return this->match([](found fo) { return std::make_optional(fo); },
                               [](not_found) { return std::nullopt; },
                               [](error err) {
                                   handle_error(err);
                                   return std::nullopt;
                               });
        }

    private:
        static void handle_error(error err);
    };

    matcher& reload_input(string_fragment sf, int next_offset)
    {
        this->mb_input = input{sf, next_offset, next_offset};

        return *this;
    }

    bool found_p(uint32_t options = 0);

    matches_result matches(uint32_t options = 0);

    int get_next_offset() const { return this->mb_input.i_next_offset; }

private:
    friend capture_builder;

    matcher(const code& co, input& in, match_data& md)
        : mb_code(co), mb_input(in), mb_match_data(md)
    {
    }

    const code& mb_code;
    input mb_input;
    match_data& mb_match_data;
};

struct capture_builder {
    const code& mb_code;
    input mb_input;
    uint32_t mb_options{0};

    capture_builder at(const string_fragment& remaining) &&
    {
        this->mb_input.i_offset = this->mb_input.i_next_offset
            = remaining.sf_begin;
        return *this;
    }

    capture_builder with_options(uint32_t opts) &&
    {
        this->mb_options = opts;
        return *this;
    }

    matcher into(match_data& md) &&;

    template<uint32_t Options = 0, typename F>
    Result<string_fragment, matcher::error> for_each(F func) &&;
};

struct compile_error {
    std::string ce_pattern;
    int ce_code{0};
    size_t ce_offset{0};

    std::string get_message() const;
};

class code {
public:
    class named_capture {
    public:
        size_t get_index() const;
        string_fragment get_name() const;

        PCRE2_SPTR nc_entry;
    };

    class named_captures {
    public:
        struct iterator {
            named_capture operator*() const;
            iterator& operator++();
            bool operator==(const iterator& other) const;
            bool operator!=(const iterator& other) const;

            uint32_t i_entry_size;
            PCRE2_SPTR i_entry;
        };

        iterator begin() const;
        iterator end() const;
        bool empty() const { return this->nc_count == 0; }
        size_t size() const { return this->nc_count; }

    private:
        friend code;

        named_captures() = default;

        uint32_t nc_count{0};
        uint32_t nc_entry_size{0};
        PCRE2_SPTR nc_name_table{nullptr};
    };

    static Result<code, compile_error> from(string_fragment sf,
                                            int options = 0);

    template<typename T, std::size_t N>
    static code from_const(const T (&str)[N], int options = 0)
    {
        auto res = from(string_fragment::from_const(str), options);

        if (res.isErr()) {
            fprintf(stderr, "failed to compile constant regex: %s\n", str);
            fprintf(stderr, "  %s\n", res.unwrapErr().get_message().c_str());
        }

        return res.unwrap();
    }

    const std::string& get_pattern() const { return this->p_pattern; }

    std::string to_string() const { return this->p_pattern; }

    named_captures get_named_captures() const;

    const char* get_name_for_capture(size_t index) const;

    size_t get_capture_count() const;

    int name_index(const char* name) const;

    std::vector<string_fragment> get_captures() const;

    uint32_t get_match_data_capacity() const
    {
        return this->p_match_proto.md_ovector_count;
    }

    match_data create_match_data() const;

    capture_builder capture_from(string_fragment in) const
    {
        return capture_builder{
            *this,
            input{in},
        };
    }

    matcher::matches_result find_in(string_fragment in,
                                    uint32_t options = 0) const
    {
        thread_local match_data md = this->create_match_data();

        if (md.md_ovector_count < this->p_match_proto.md_ovector_count) {
            md = this->create_match_data();
        }

        return this->capture_from(in).into(md).matches(options);
    }

    size_t match_partial(string_fragment in) const;

    std::string replace(string_fragment str, const char* repl) const;

    std::shared_ptr<code> to_shared() &&
    {
        return std::make_shared<code>(std::move(this->p_code),
                                      std::move(this->p_pattern));
    }

    code(auto_mem<pcre2_code> code, std::string pattern)
        : p_code(std::move(code)), p_pattern(std::move(pattern)),
          p_match_proto(this->create_match_data())
    {
    }

private:
    friend matcher;
    friend match_data;

    auto_mem<pcre2_code> p_code;
    std::string p_pattern;
    match_data p_match_proto;
};

template<typename T, std::size_t N>
std::optional<string_fragment>
match_data::operator[](const T (&name)[N]) const
{
    auto index = pcre2_substring_number_from_name(
        this->md_code->p_code.in(),
        reinterpret_cast<const unsigned char*>(name));

    return this->operator[](index);
}

template<uint32_t Options, typename F>
Result<string_fragment, matcher::error>
capture_builder::for_each(F func) &&
{
    thread_local auto md = match_data::unitialized();

    if (md.get_capacity() < this->mb_code.get_match_data_capacity()) {
        md = this->mb_code.create_match_data();
    }
    auto mat = matcher{this->mb_code, this->mb_input, md};

    bool done = false;
    matcher::error eret;

    while (!done) {
        auto match_res = mat.matches(Options | this->mb_options);
        done = match_res.match(
            [mat, &func](matcher::found) {
                func(mat.mb_match_data);
                return false;
            },
            [](matcher::not_found) { return true; },
            [&eret](matcher::error err) {
                eret = err;
                return true;
            });
    }

    if (eret.e_error_code == 0) {
        return Ok(md.remaining());
    }
    return Err(eret);
}

}  // namespace lnav::pcre2pp

#endif
