/**
 * Copyright (c) 2020, Timothy Stack
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

#include "help_text.hh"

#include "config.h"

help_text&
help_text::with_parameters(
    const std::initializer_list<help_text>& params) noexcept
{
    this->ht_parameters = params;
    for (auto& param : this->ht_parameters) {
        param.ht_context = help_context_t::HC_PARAMETER;
    }
    return *this;
}

help_text&
help_text::with_parameter(const help_text& ht) noexcept
{
    this->ht_parameters.emplace_back(ht);
    this->ht_parameters.back().ht_context = help_context_t::HC_PARAMETER;
    return *this;
}

help_text&
help_text::with_result(const help_text& ht) noexcept
{
    this->ht_results.emplace_back(ht);
    this->ht_results.back().ht_context = help_context_t::HC_RESULT;
    return *this;
}

help_text&
help_text::with_examples(
    const std::initializer_list<help_example>& examples) noexcept
{
    this->ht_example = examples;
    return *this;
}

help_text&
help_text::with_example(const help_example& example) noexcept
{
    this->ht_example.emplace_back(example);
    return *this;
}

help_text&
help_text::with_enum_values(
    const std::initializer_list<const char*>& enum_values) noexcept
{
    this->ht_enum_values = enum_values;
    return *this;
}

help_text&
help_text::with_tags(const std::initializer_list<const char*>& tags) noexcept
{
    this->ht_tags = tags;
    return *this;
}

help_text&
help_text::with_opposites(
    const std::initializer_list<const char*>& opps) noexcept
{
    this->ht_opposites = opps;
    return *this;
}

help_text&
help_text::with_prql_path(
    const std::initializer_list<const char*>& prql) noexcept
{
    this->ht_prql_path = prql;
    return *this;
}

void
help_text::index_tags()
{
    for (const auto& tag : this->ht_tags) {
        TAGGED.insert(std::make_pair(tag, this));
    }
}
