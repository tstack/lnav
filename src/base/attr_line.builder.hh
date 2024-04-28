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

#ifndef lnav_attr_line_builder_hh
#define lnav_attr_line_builder_hh

#include <utility>

#include "attr_line.hh"

class attr_line_builder {
public:
    explicit attr_line_builder(attr_line_t& al) : alb_line(al) {}

    class attr_guard {
    public:
        explicit attr_guard(attr_line_t& al)
            : ag_line(al), ag_start(std::nullopt)
        {
        }

        attr_guard(attr_line_t& al, string_attr_pair sap)
            : ag_line(al), ag_start(al.get_string().length()),
              ag_attr(std::move(sap))
        {
        }

        attr_guard(const attr_guard&) = delete;

        attr_guard& operator=(const attr_guard&) = delete;

        attr_guard(attr_guard&& other) noexcept
            : ag_line(other.ag_line), ag_start(std::move(other.ag_start)),
              ag_attr(std::move(other.ag_attr))
        {
            other.ag_start = std::nullopt;
        }

        ~attr_guard()
        {
            if (this->ag_start) {
                this->ag_line.al_attrs.emplace_back(
                    line_range{
                        this->ag_start.value(),
                        (int) this->ag_line.get_string().length(),
                    },
                    this->ag_attr);
            }
        }

    private:
        attr_line_t& ag_line;
        std::optional<int> ag_start;
        string_attr_pair ag_attr;
    };

    attr_guard with_default() { return attr_guard{this->alb_line}; }

    attr_guard with_attr(string_attr_pair sap)
    {
        return {this->alb_line, std::move(sap)};
    }

    template<typename... Args>
    attr_line_builder& overlay_attr(Args... args)
    {
        this->alb_line.al_attrs.template emplace_back(args...);
        return *this;
    }

    template<typename... Args>
    attr_line_builder& overlay_attr_for_char(int index, Args... args)
    {
        this->alb_line.al_attrs.template emplace_back(
            line_range{index, index + 1}, args...);
        return *this;
    }

    template<typename... Args>
    attr_line_builder& append(Args... args)
    {
        this->alb_line.append(args...);

        return *this;
    }

    template<typename... Args>
    attr_line_builder& appendf(Args... args)
    {
        this->alb_line.appendf(args...);

        return *this;
    }

    attr_line_builder& indent(size_t amount)
    {
        auto pre = this->with_attr(SA_PREFORMATTED.value());

        this->append(amount, ' ');

        return *this;
    }

    attr_line_builder& append_as_hexdump(const string_fragment& sf);

private:
    attr_line_t& alb_line;
};

#endif
