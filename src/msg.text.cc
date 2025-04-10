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

#include "msg.text.hh"

#include "base/lnav.console.hh"

using namespace lnav::roles::literals;

namespace lnav::messages::view {

const std::vector<attr_line_t>*
no_files()
{
    static const auto retval
        = lnav::console::user_message::info(
              "No log or text files are currently loaded")
              .with_help(attr_line_t("Use the ")
                             .append(":open"_keyword)
                             .append(" command to open a file or directory"))
              .to_attr_line()
              .split_lines();

    return &retval;
}

const std::vector<attr_line_t>*
only_text_files()
{
    static const auto retval
        = lnav::console::user_message::info(
              "Only text files are currently loaded, they have not been "
              "detected as log files")
              .with_note(
                  "Check the Files panel below to get more details on why the "
                  "files are treated as text")
              .with_help(attr_line_t("Press '")
                             .append("t"_hotkey)
                             .append("' to switch to the TEXT view"))
              .to_attr_line()
              .split_lines();

    return &retval;
}

const std::vector<attr_line_t>*
only_log_files()
{
    static const auto retval
        = lnav::console::user_message::info(
              "All loaded files have been detected as logs, there are no plain "
              "text files")
              .with_help(attr_line_t("Press '")
                             .append("q"_hotkey)
                             .append("' to exit this view"))
              .to_attr_line()
              .split_lines();

    return &retval;
}

const std::vector<attr_line_t>*
empty_file()
{
    static const auto retval
        = lnav::console::user_message::info(
              "File is empty, content will be shown when added")
              .to_attr_line()
              .split_lines();

    return &retval;
}

}  // namespace lnav::messages::view
