/**
 * Copyright (c) 2023, Timothy Stack
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

#include "piper.file.hh"

#include <arpa/inet.h>
#include <unistd.h>

#include "base/injector.hh"
#include "base/lnav_log.hh"
#include "base/paths.hh"
#include "base/snippet_highlighters.hh"
#include "piper.looper.cfg.hh"
#include "readline_highlighters.hh"

using namespace lnav::roles::literals;

namespace lnav {
namespace piper {

const char HEADER_MAGIC[4] = {'L', 0, 'N', 1};

const std::filesystem::path&
storage_path()
{
    static auto INSTANCE = lnav::paths::workdir() / "piper";

    return INSTANCE;
}

std::optional<auto_buffer>
read_header(int fd, const char* first8)
{
    if (memcmp(first8, HEADER_MAGIC, sizeof(HEADER_MAGIC)) != 0) {
        log_trace("first 4 bytes are not a piper header: %02x%02x%02x%02x",
                  first8[0],
                  first8[1],
                  first8[2],
                  first8[3]);
        return std::nullopt;
    }

    uint32_t meta_size = ntohl(*((uint32_t*) &first8[4]));

    auto meta_buf = auto_buffer::alloc(meta_size);
    if (meta_buf.in() == nullptr) {
        log_error("failed to alloc %d bytes for header", meta_size);
        return std::nullopt;
    }
    auto meta_prc = pread(fd, meta_buf.in(), meta_size, 8);
    if (meta_prc != meta_size) {
        log_error("failed to read piper header: %s", strerror(errno));
        return std::nullopt;
    }
    meta_buf.resize(meta_size);

    return meta_buf;
}

multiplex_matcher::match_result
multiplex_matcher::match(const string_fragment& line)
{
    const auto& cfg = injector::get<const config&>();
    auto md = lnav::pcre2pp::match_data::unitialized();

    for (const auto& demux_pair : cfg.c_demux_definitions) {
        const auto& df = demux_pair.second;

        if (!df.dd_valid) {
            continue;
        }

        if (!this->mm_partial_match_ids.empty()
            && this->mm_partial_match_ids.count(demux_pair.first) == 0)
        {
            continue;
        }

        log_info("attempting to demux using: %s", demux_pair.first.c_str());
        {
            md = df.dd_pattern.pp_value->create_match_data();
            if (df.dd_pattern.pp_value->capture_from(line)
                    .into(md)
                    .matches()
                    .ignore_error())
            {
                log_info("  demuxer pattern matched");
                if (!md[df.dd_muxid_capture_index].has_value()) {
                    log_info("    however, mux_id was not captured");

                    const auto match_um = lnav::console::user_message::warning(
                        attr_line_t("demuxer ")
                            .append_quoted(demux_pair.first)
                            .append(" matched, however the ")
                            .append("mux_id"_symbol)
                            .append(" was not captured"));
                    this->mm_details.emplace_back(match_um);
                    continue;
                }
                if (!md[df.dd_body_capture_index].has_value()) {
                    log_info("    however, body was not captured");
                    const auto match_um = lnav::console::user_message::warning(
                        attr_line_t("demuxer ")
                            .append_quoted(demux_pair.first)
                            .append(" matched, however the ")
                            .append("body"_symbol)
                            .append(" was not captured"));
                    this->mm_details.emplace_back(match_um);
                    continue;
                }
                log_info("  and required captures were found, using demuxer");

                if (df.dd_enabled) {
                    auto match_um = lnav::console::user_message::ok(
                        attr_line_t("demuxer ")
                            .append_quoted(demux_pair.first)
                            .append(" matched line ")
                            .append(lnav::roles::number(
                                fmt::to_string(this->mm_line_count))));
                    this->mm_details.emplace_back(match_um);
                    return found{demux_pair.first};
                }
                auto config_al
                    = attr_line_t()
                          .append(fmt::format(
                              FMT_STRING(":config /log/demux/{}/enabled "
                                         "true"),
                              demux_pair.first))
                          .move();
                readline_lnav_highlighter(config_al, -1);
                auto match_um
                    = lnav::console::user_message::info(
                          attr_line_t("demuxer ")
                              .append_quoted(demux_pair.first)
                              .append(" matched line ")
                              .append(lnav::roles::number(
                                  fmt::to_string(this->mm_line_count)))
                              .append(", however, it is disabled"))
                          .with_help(
                              attr_line_t("Use ")
                                  .append_quoted(
                                      lnav::roles::quoted_code(config_al))
                                  .append(" to enable this demuxer"))
                          .move();
                this->mm_details.emplace_back(match_um);
            }

            auto partial_size = df.dd_pattern.pp_value->match_partial(
                line.sub_range(0, 1024));
            auto regex_al = attr_line_t(df.dd_pattern.pp_value->get_pattern());
            lnav::snippets::regex_highlighter(
                regex_al, -1, line_range{0, (int) regex_al.length()});
            auto in_line = line.sub_range(0, 1024).rtrim("\n").to_string();
            auto esc_res
                = fmt::v10::detail::find_escape(&in_line[0], &(*in_line.end()));
            if (esc_res.end != nullptr) {
                in_line = fmt::format(FMT_STRING("{:?}"), in_line);
            }
            auto note = attr_line_t("pattern: ")
                            .append(regex_al)
                            .append("\n  ")
                            .append(lnav::roles::quoted_code(in_line))
                            .append("\n")
                            .append(partial_size + 2, ' ')
                            .append("^ matched up to here")
                            .move();
            auto match_um = lnav::console::user_message::info(
                                attr_line_t("demuxer ")
                                    .append_quoted(demux_pair.first)
                                    .append(" did not match line ")
                                    .append(lnav::roles::number(
                                        fmt::to_string(this->mm_line_count))))
                                .with_note(note)
                                .move();
            this->mm_details.emplace_back(match_um);
        }
        if (df.dd_control_pattern.pp_value) {
            md = df.dd_control_pattern.pp_value->create_match_data();
            if (df.dd_control_pattern.pp_value->capture_from(line)
                    .into(md)
                    .matches()
                    .ignore_error())
            {
                log_info("  demuxer control pattern matched");
                this->mm_partial_match_ids.emplace(demux_pair.first);
            }
        }
    }

    this->mm_line_count += 1;
    if (this->mm_partial_match_ids.empty()) {
        return not_found{};
    }

    return partial{};
}

}  // namespace piper
}  // namespace lnav
