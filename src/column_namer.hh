/**
 * Copyright (c) 2013, Timothy Stack
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
 *
 * @file column_namer.hh
 */

#ifndef _column_namer_hh
#define _column_namer_hh

#include <map>
#include <string>
#include <vector>
#include <algorithm>

#include "sql_util.hh"
#include "lnav_util.hh"

class column_namer {
public:
    column_namer()
    {
        this->cn_builtin_names.push_back("col");
    };

    bool existing_name(const std::string &in_name) const
    {
        if (std::binary_search(std::begin(sql_keywords),
                               std::end(sql_keywords),
                               toupper(in_name))) {
            return true;
        }

        if (find(this->cn_builtin_names.begin(),
                 this->cn_builtin_names.end(),
                 in_name) != this->cn_builtin_names.end()) {
            return true;
        }

        if (find(this->cn_names.begin(),
                 this->cn_names.end(),
                 in_name) != this->cn_names.end()) {
            return true;
        }

        return false;
    };

    std::string add_column(const std::string &in_name)
    {
        std::string base_name = in_name, retval;
        size_t      buf_size;
        int         num = 0;

        buf_size = in_name.length() + 64;
        char buffer[buf_size];
        if (in_name == "") {
            base_name = "col";
        }

        retval = base_name;
        while (this->existing_name(retval)) {
            snprintf(buffer, buf_size, "%s_%d", base_name.c_str(), num);
            retval = buffer;
            num   += 1;
        }

        this->cn_names.push_back(retval);

        return retval;
    };

    std::vector<std::string> cn_builtin_names;
    std::vector<std::string> cn_names;
};
#endif
