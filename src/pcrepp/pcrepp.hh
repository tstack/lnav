/**
 * Copyright (c) 2007-2013, Timothy Stack
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
 * @file pcrepp.hh
 *
 * A C++ adapter for the pcre library.  The interface provided here has a
 * different focus than the pcrecpp.h file included in the pcre distribution.
 * The standard pcrecpp.h interface is more concerned with regular expressions
 * that are digesting data to be used within the program itself.  Whereas this
 * interface is dealing with regular expression entered by the user and
 * processing a series of matches on text files.
 */

#ifndef pcrepp_hh
#define pcrepp_hh

#include "config.h"

#ifdef HAVE_PCRE_H
#    include <pcre.h>
#elif HAVE_PCRE_PCRE_H
#    include <pcre/pcre.h>
#else
#    error "pcre.h not found?"
#endif

#include <cassert>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <stdio.h>
#include <string.h>

#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "base/result.h"

class pcrepp;

/**
 * Context that tracks captures found during a match operation.  This class is a
 * base that defines iterator methods and fields, but does not allocate space
 * for the capture array.
 */
class pcre_context {
public:
    struct capture_t {
        capture_t()
        { /* We don't initialize anything since it's a perf hit. */
        }

        capture_t(int begin, int end) : c_begin(begin), c_end(end)
        {
            assert(begin <= end);
        }

        int c_begin;
        int c_end;

        void ltrim(const char* str);

        bool contains(int pos) const
        {
            return this->c_begin <= pos && pos < this->c_end;
        }

        bool is_valid() const { return this->c_begin != -1; }

        int length() const { return this->c_end - this->c_begin; }

        bool empty() const { return this->c_begin == this->c_end; }
    };
    using iterator = capture_t*;
    using const_iterator = const capture_t*;

    /** @return The maximum number of strings this context can capture. */
    int get_max_count() const { return this->pc_max_count; }

    void set_count(int count) { this->pc_count = count; }

    int get_count() const { return this->pc_count; }

    void set_pcrepp(const pcrepp* src) { this->pc_pcre = src; }

    /**
     * @return a capture_t that covers all of the text that was matched.
     */
    capture_t* all() const { return pc_captures; }

    /** @return An iterator to the first capture. */
    iterator begin() { return pc_captures + 1; }
    /** @return An iterator that refers to the end of the capture array. */
    iterator end() { return pc_captures + pc_count; };

    capture_t* operator[](int offset) const
    {
        if (offset < 0) {
            return nullptr;
        }
        return &this->pc_captures[offset + 1];
    }

    capture_t* operator[](const char* name) const;

    capture_t* operator[](const std::string& name) const
    {
        return (*this)[name.c_str()];
    }

    capture_t* first_valid() const;

protected:
    pcre_context(capture_t* captures, int max_count)
        : pc_captures(captures), pc_max_count(max_count)
    {
    }

    const pcrepp* pc_pcre{nullptr};
    capture_t* pc_captures;
    int pc_max_count;
    int pc_count{0};
};

struct capture_if_not {
    capture_if_not(int begin) : cin_begin(begin) {}

    bool operator()(const pcre_context::capture_t& cap) const
    {
        return cap.c_begin != this->cin_begin;
    }

    int cin_begin;
};

/**
 * A pcre_context that allocates storage for the capture array within the object
 * itself.
 */
template<size_t MAX_COUNT>
class pcre_context_static : public pcre_context {
public:
    pcre_context_static()
        : pcre_context(this->pc_match_buffer, MAX_COUNT + 1){};

private:
    capture_t pc_match_buffer[MAX_COUNT + 1];
};

/**
 *
 */
class pcre_input {
public:
    pcre_input(const char* str, size_t off = 0, size_t len = -1)
        : pi_offset(off), pi_next_offset(off), pi_length(len), pi_string(str)
    {
        if (this->pi_length == (size_t) -1) {
            this->pi_length = strlen(str);
        }
    }

    pcre_input(const string_fragment& s)
        : pi_offset(0), pi_next_offset(0), pi_length(s.length()),
          pi_string(s.data())
    {
    }

    pcre_input(const intern_string_t& s)
        : pi_offset(0), pi_next_offset(0), pi_length(s.size()),
          pi_string(s.get())
    {
    }

    pcre_input(const string_fragment&&) = delete;

    pcre_input(const std::string& str, size_t off = 0)
        : pi_offset(off), pi_next_offset(off), pi_length(str.length()),
          pi_string(str.c_str())
    {
    }

    pcre_input(const std::string&&, size_t off = 0) = delete;

    const char* get_string() const { return this->pi_string; }

    const char* get_substr_start(pcre_context::const_iterator iter) const
    {
        return &this->pi_string[iter->c_begin];
    }

    size_t get_substr_len(pcre_context::const_iterator iter) const
    {
        return iter->length();
    }

    std::string get_substr(pcre_context::const_iterator iter) const
    {
        if (iter->c_begin == -1) {
            return "";
        }
        return std::string(&this->pi_string[iter->c_begin], iter->length());
    }

    intern_string_t get_substr_i(pcre_context::const_iterator iter) const
    {
        return intern_string::lookup(&this->pi_string[iter->c_begin],
                                     iter->length());
    }

    string_fragment get_string_fragment(pcre_context::const_iterator iter) const
    {
        return string_fragment::from_byte_range(
            this->pi_string, iter->c_begin, iter->c_end);
    }

    string_fragment get_up_to(pcre_context::const_iterator iter) const
    {
        return string_fragment::from_byte_range(
            this->pi_string, this->pi_offset, iter->c_begin);
    }

    nonstd::optional<std::string> get_substr_opt(
        pcre_context::const_iterator iter) const
    {
        if (iter->is_valid()) {
            return std::string(&this->pi_string[iter->c_begin], iter->length());
        }

        return nonstd::nullopt;
    }

    void get_substr(pcre_context::const_iterator iter, char* dst) const
    {
        memcpy(dst, &this->pi_string[iter->c_begin], iter->length());
        dst[iter->length()] = '\0';
    }

    void reset_next_offset() { this->pi_next_offset = this->pi_offset; }

    void reset(const char* str, size_t off = 0, size_t len = -1)
    {
        this->pi_string = str;
        this->pi_offset = off;
        this->pi_next_offset = off;
        if (this->pi_length == (size_t) -1) {
            this->pi_length = strlen(str);
        } else {
            this->pi_length = len;
        }
    }

    void reset(const std::string& str, size_t off = 0)
    {
        this->reset(str.c_str(), off, str.length());
    }

    size_t pi_offset;
    size_t pi_next_offset;
    size_t pi_length;

private:
    const char* pi_string;
};

struct pcre_named_capture {
    class iterator {
    public:
        iterator(pcre_named_capture* pnc, size_t name_len)
            : i_named_capture(pnc), i_name_len(name_len)
        {
        }

        iterator() : i_named_capture(nullptr), i_name_len(0) {}

        const pcre_named_capture& operator*() const
        {
            return *this->i_named_capture;
        }

        const pcre_named_capture* operator->() const
        {
            return this->i_named_capture;
        }

        bool operator!=(const iterator& rhs) const
        {
            return this->i_named_capture != rhs.i_named_capture;
        }

        iterator& operator++()
        {
            char* ptr = (char*) this->i_named_capture;

            ptr += this->i_name_len;
            this->i_named_capture = (pcre_named_capture*) ptr;
            return *this;
        }

    private:
        pcre_named_capture* i_named_capture;
        size_t i_name_len;
    };

    int index() const
    {
        return (this->pnc_index_msb << 8 | this->pnc_index_lsb) - 1;
    }

    char pnc_index_msb;
    char pnc_index_lsb;
    char pnc_name[];
};

struct pcre_extractor {
    const pcre_context& pe_context;
    const pcre_input& pe_input;

    template<typename T>
    intern_string_t get_substr_i(T name) const
    {
        return this->pe_input.get_substr_i(this->pe_context[name]);
    }

    template<typename T>
    std::string get_substr(T name) const
    {
        return this->pe_input.get_substr(this->pe_context[name]);
    }
};

class pcrepp {
public:
    class error : public std::exception {
    public:
        error(std::string msg, int offset = 0)
            : e_msg(std::move(msg)), e_offset(offset)
        {
        }

        const char* what() const noexcept override
        {
            return this->e_msg.c_str();
        }

        const std::string e_msg;
        int e_offset;
    };

    static std::string quote(const char* unquoted);

    static std::string quote(const std::string& unquoted)
    {
        return quote(unquoted.c_str());
    }

    struct compile_error {
        const char* ce_msg{nullptr};
        int ce_offset{0};
    };

    static Result<pcrepp, compile_error> from_str(std::string pattern,
                                                  int options = 0);

    pcrepp(pcre* code) : p_code(code), p_code_extra(pcre_free_study)
    {
        pcre_refcount(this->p_code, 1);
        this->study();
    }

    pcrepp(std::string pattern, pcre* code)
        : p_code(code), p_pattern(std::move(pattern)),
          p_code_extra(pcre_free_study)
    {
        pcre_refcount(this->p_code, 1);
        this->study();
        this->find_captures(this->p_pattern.c_str());
    }

    explicit pcrepp(const char* pattern, int options = 0)
        : p_pattern(pattern), p_code_extra(pcre_free_study)
    {
        const char* errptr;
        int eoff;

        if ((this->p_code
             = pcre_compile(pattern, options, &errptr, &eoff, nullptr))
            == nullptr)
        {
            throw error(errptr, eoff);
        }

        pcre_refcount(this->p_code, 1);
        this->study();
        this->find_captures(pattern);
    }

    explicit pcrepp(const std::string& pattern, int options = 0)
        : p_pattern(pattern), p_code_extra(pcre_free_study)
    {
        const char* errptr;
        int eoff;

        if ((this->p_code = pcre_compile(
                 pattern.c_str(), options | PCRE_UTF8, &errptr, &eoff, nullptr))
            == nullptr)
        {
            throw error(errptr, eoff);
        }

        pcre_refcount(this->p_code, 1);
        this->study();
        this->find_captures(pattern.c_str());
    }

    pcrepp() {}

    pcrepp(const pcrepp& other)
        : p_code(other.p_code), p_pattern(other.p_pattern),
          p_code_extra(pcre_free_study), p_captures(other.p_captures)
    {
        pcre_refcount(this->p_code, 1);
        this->study();
    }

    pcrepp(pcrepp&& other)
        : p_code(other.p_code), p_pattern(std::move(other.p_pattern)),
          p_code_extra(pcre_free_study), p_capture_count(other.p_capture_count),
          p_named_count(other.p_named_count), p_name_len(other.p_name_len),
          p_options(other.p_options), p_named_entries(other.p_named_entries),
          p_captures(std::move(other.p_captures))
    {
        pcre_refcount(this->p_code, 1);
        this->p_code_extra = std::move(other.p_code_extra);
    }

    virtual ~pcrepp() { this->clear(); }

    pcrepp& operator=(pcrepp&& other) noexcept
    {
        if (this == &other) {
            return *this;
        }

        this->p_code = other.p_code;
        pcre_refcount(this->p_code, 1);
        this->p_pattern = std::move(other.p_pattern);
        this->p_code_extra = std::move(other.p_code_extra);
        this->p_capture_count = other.p_capture_count;
        this->p_named_count = other.p_named_count;
        this->p_name_len = other.p_name_len;
        this->p_options = other.p_options;
        this->p_named_entries = other.p_named_entries;
        this->p_captures = std::move(other.p_captures);

        return *this;
    }

    const std::string& get_pattern() const
    {
        return this->p_pattern;
    }

    bool empty() const
    {
        return this->p_pattern.empty();
    }

    void clear()
    {
        if (this->p_code && pcre_refcount(this->p_code, -1) == 0) {
            free(this->p_code);
            this->p_code = nullptr;
        }
        this->p_pattern.clear();
        this->p_code_extra.reset();
        this->p_capture_count = 0;
        this->p_named_count = 0;
        this->p_name_len = 0;
        this->p_options = 0;
        this->p_named_entries = nullptr;
        this->p_captures.clear();
    }

    pcre_named_capture::iterator named_begin() const
    {
        return {this->p_named_entries, static_cast<size_t>(this->p_name_len)};
    }

    pcre_named_capture::iterator named_end() const
    {
        char* ptr = (char*) this->p_named_entries;

        ptr += this->p_named_count * this->p_name_len;
        return {(pcre_named_capture*) ptr,
                static_cast<size_t>(this->p_name_len)};
    }

    const std::vector<pcre_context::capture_t>& captures() const
    {
        return this->p_captures;
    }

    std::vector<pcre_context::capture_t>::const_iterator cap_begin() const
    {
        return this->p_captures.begin();
    }

    std::vector<pcre_context::capture_t>::const_iterator cap_end() const
    {
        return this->p_captures.end();
    }

    int name_index(const std::string& name) const
    {
        return this->name_index(name.c_str());
    }

    int name_index(const char* name) const;

    const char* name_for_capture(int index) const;

    int get_capture_count() const { return this->p_capture_count; }

    bool match(pcre_context& pc, pcre_input& pi, int options = 0) const;

    template<size_t MATCH_COUNT>
    nonstd::optional<pcre_context_static<MATCH_COUNT>> match(pcre_input& pi,
                                                             int options
                                                             = 0) const
    {
        pcre_context_static<MATCH_COUNT> pc;

        if (this->match(pc, pi, options)) {
            return pc;
        }

        return nonstd::nullopt;
    }

    std::string replace(const char* str, const char* repl) const;

    size_t match_partial(pcre_input& pi) const;

// #undef PCRE_STUDY_JIT_COMPILE
#ifdef PCRE_STUDY_JIT_COMPILE
    static pcre_jit_stack* jit_stack();

#else
    static void pcre_free_study(pcre_extra*);
#endif

    void study();

    void find_captures(const char* pattern);

    pcre* p_code{nullptr};
    std::string p_pattern;
    auto_mem<pcre_extra> p_code_extra;
    int p_capture_count{0};
    int p_named_count{0};
    int p_name_len{0};
    unsigned long p_options{0};
    pcre_named_capture* p_named_entries{nullptr};
    std::vector<pcre_context::capture_t> p_captures;
};

template<int options = 0>
class pcrepp_with_options : public pcrepp {
public:
    template<typename... Args>
    pcrepp_with_options(Args... args) : pcrepp(args..., options)
    {
    }
};

#endif
