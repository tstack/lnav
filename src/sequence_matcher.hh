/**
 * Copyright (c) 2007-2012, Timothy Stack
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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __sequence_matcher_hh
#define __sequence_matcher_hh

#include <list>
#include <string>
#include <vector>

#include "byte_array.hh"

class sequence_matcher {
public:
    typedef std::vector<std::string> field_row_t;
    typedef std::list<field_row_t>   field_col_t;

    typedef byte_array<2, uint64_t>    id_t;

    enum field_type_t {
        FT_VARIABLE,
        FT_CONSTANT,
    };

    struct field {
        field() : sf_type(FT_VARIABLE) { };

        field_type_t sf_type;
        field_row_t  sf_value;
    };

    sequence_matcher(field_col_t &example);

    void identity(const std::vector<std::string> &values, id_t &id_out);

    template<typename T>
    bool match(const std::vector<std::string> &values,
               std::vector<T> &state,
               T index)
    {
        bool index_match = true;
        int  lpc         = 0;

retry:
        for (std::list<field>::iterator iter = this->sm_fields.begin();
             iter != this->sm_fields.end();
             ++iter, lpc++) {
            if (iter->sf_type != sequence_matcher::FT_CONSTANT) {
                continue;
            }

            if (iter->sf_value[state.size()] != values[lpc]) {
                if (!state.empty()) {
                    state.clear();
                    lpc = 0;
                    goto retry;
                }
                else {
                    index_match = false;
                    break;
                }
            }
        }

        if (index_match) {
            state.push_back(index);
        }

        return (size_t)this->sm_count == state.size();
    };

private:
    int sm_count;
    std::list<field> sm_fields;
};
#endif
