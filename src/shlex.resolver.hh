/**
 * Copyright (c) 2015, Timothy Stack
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
 * @file shlex.resolver.hh
 */

#ifndef lnav_shlex_resolver_hh
#define lnav_shlex_resolver_hh

#include <map>
#include <string>
#include <vector>

class scoped_resolver {
public:
    scoped_resolver(
        std::initializer_list<std::map<std::string, std::string>*> l)
    {
        this->sr_stack.insert(this->sr_stack.end(), l.begin(), l.end());
    };

    typedef std::map<std::string, std::string>::const_iterator const_iterator;

    const_iterator find(const std::string& str) const
    {
        const_iterator retval;

        for (auto scope : this->sr_stack) {
            if ((retval = scope->find(str)) != scope->end()) {
                return retval;
            }
        }

        return this->end();
    };

    const_iterator end() const
    {
        return this->sr_stack.back()->end();
    }

    std::vector<const std::map<std::string, std::string>*> sr_stack;
};

#endif
