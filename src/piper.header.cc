/**
* Copyright (c) 2024, Timothy Stack
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

#include "piper.header.hh"

#include "yajlpp/yajlpp_def.hh"

namespace lnav::piper {

static const json_path_container header_env_handlers = {
    yajlpp::pattern_property_handler("(?<name>.*)")
        .with_synopsis("<name>")
        .for_field(&lnav::piper::header::h_env),
};

static const json_path_container header_demux_handlers = {
    yajlpp::pattern_property_handler("(?<name>.*)")
        .with_synopsis("<name>")
        .for_field(&lnav::piper::header::h_demux_meta),
};

static const json_path_handler_base::enum_value_t demux_output_values[] = {
    {"not_applicable", demux_output_t::not_applicable},
    {"signal", demux_output_t::signal},
    {"invalid", demux_output_t::invalid},

    json_path_handler_base::ENUM_TERMINATOR,
};

const typed_json_path_container<lnav::piper::header> header_handlers = {
    yajlpp::property_handler("name").for_field(&lnav::piper::header::h_name),
    yajlpp::property_handler("timezone")
        .for_field(&lnav::piper::header::h_timezone),
    yajlpp::property_handler("ctime").for_field(&lnav::piper::header::h_ctime),
    yajlpp::property_handler("cwd").for_field(&lnav::piper::header::h_cwd),
    yajlpp::property_handler("env").with_children(header_env_handlers),
    yajlpp::property_handler("mux_id").for_field(
        &lnav::piper::header::h_mux_id),
    yajlpp::property_handler("demux_output")
        .with_enum_values(demux_output_values)
        .for_field(&lnav::piper::header::h_demux_output),
    yajlpp::property_handler("demux_meta").with_children(header_demux_handlers),
};

}
