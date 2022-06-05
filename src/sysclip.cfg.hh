/**
 * Copyright (c) 2021, Timothy Stack
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
 * @file sysclip.cfg.hh
 */

#ifndef lnav_sysclip_cfg_hh
#define lnav_sysclip_cfg_hh

#include <map>
#include <string>

#include "base/lnav_log.hh"
#include "sysclip.hh"

namespace sysclip {

struct clip_commands {
    std::string cc_write;
    std::string cc_read;

    std::string select(op_t op) const
    {
        switch (op) {
            case op_t::WRITE:
                return this->cc_write;
            case op_t::READ:
                return this->cc_read;
        }

        ensure(false);
    }
};

struct clipboard {
    std::string c_test_command;
    clip_commands c_general;
    clip_commands c_find;

    const clip_commands& select(type_t t) const
    {
        switch (t) {
            case type_t::GENERAL:
                return this->c_general;
            case type_t::FIND:
                return this->c_find;
        }

        ensure(false);
    }
};

struct config {
    std::map<std::string, clipboard> c_clipboard_impls;
};

}  // namespace sysclip

#endif
