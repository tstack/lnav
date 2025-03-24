/**
 * Copyright (c) 2025, Timothy Stack
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

#include "lnav.script.parser.hh"

namespace lnav::script {

Result<void, lnav::console::user_message>
parser::push_back(string_fragment line)
{
    this->p_line_number += 1;

    if (line.trim().empty()) {
        if (this->p_cmdline) {
            this->p_cmdline = this->p_cmdline.value() + "\n";
        }
        return Ok();
    }
    if (line[0] == '#') {
        return Ok();
    }

    switch (line[0]) {
        case ':':
        case '/':
        case ';':
        case '|':
            if (this->p_cmdline) {
                TRY(this->handle_command(trim(this->p_cmdline.value())));
            }

            this->p_starting_line_number = this->p_line_number;
            this->p_cmdline = line.to_string();
            break;
        default:
            if (this->p_cmdline) {
                this->p_cmdline = fmt::format(
                    FMT_STRING("{}{}"), this->p_cmdline.value(), line);
            } else {
                TRY(this->handle_command(fmt::format(FMT_STRING(":{}"), line)));
            }
            break;
    }

    return Ok();
}

Result<void, lnav::console::user_message>
parser::final()
{
    if (this->p_cmdline) {
        TRY(this->handle_command(trim(this->p_cmdline.value())));
    }

    return Ok();
}

}  // namespace lnav::script
