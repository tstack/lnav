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
 *
 * @file lnav_config_fwd.hh
 */

#ifndef lnav_config_fwd_hh
#define lnav_config_fwd_hh

#include <functional>
#include <string>

#include "base/intern_string.hh"
#include "base/lnav.console.hh"

class lnav_config_listener {
public:
    using error_reporter = const std::function<void(
        const void*, const lnav::console::user_message& msg)>;

    static std::vector<lnav_config_listener*>& listener_list()
    {
        static std::vector<lnav_config_listener*> retval;

        return retval;
    }

    template<typename T, std::size_t N>
    lnav_config_listener(const T (&src_file)[N])
        : lcl_name(string_fragment::from_const(src_file))
    {
        listener_list().emplace_back(this);
    }

    virtual ~lnav_config_listener();

    virtual void reload_config(error_reporter& reporter) {}

    virtual void unload_config() {}

    static void unload_all()
    {
        for (auto* lcl : listener_list()) {
            lcl->unload_config();
        }
    }

    string_fragment lcl_name;
};

#endif
