/**
 * Copyright (c) 2026, Timothy Stack
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

#ifndef lnav_file_split_hh
#define lnav_file_split_hh

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

#include "base/file_range.hh"
#include "base/lnav.console.hh"
#include "base/result.h"
#include "base/time_util.hh"

namespace lnav::file_split {

/**
 * When to start a new piece.  A piece is cut before the message that would
 * push it past any of the limits, so a single message larger than a limit
 * still ends up whole in one piece.
 */
struct limits {
    /** Lines in the file, not counting the lines lnav breaks JSON into. */
    std::optional<uint64_t> l_lines;
    std::optional<uint64_t> l_bytes;
    /** Messages are grouped into wall-clock windows of this length. */
    std::optional<std::chrono::microseconds> l_duration;
    /** Index entries per piece, so that lnav can index each piece fully. */
    uint64_t l_max_entries{0};

    bool has_criteria() const
    {
        return this->l_lines || this->l_bytes || this->l_duration;
    }

    /**
     * The limits to use when none were given: small enough to stay well
     * under the per-file line limit and to let the pieces be indexed in
     * parallel, but not so small that a worker is wasted on a tiny piece.
     *
     * @param total_bytes The size of the content, if it is known.
     * @param workers The number of files lnav indexes at once.
     * @param max_lines The most lines lnav will index in one file.
     */
    static limits defaults(std::optional<file_ssize_t> total_bytes,
                           size_t workers,
                           uint64_t max_lines);
};

struct message_info {
    file_ssize_t mi_bytes{0};
    uint64_t mi_lines{0};
    uint64_t mi_entries{0};
    std::optional<std::chrono::microseconds> mi_time;
};

class policy {
public:
    explicit policy(limits lim) : p_limits(lim) {}

    const limits& get_limits() const { return this->p_limits; }

    bool should_cut_before(const message_info& mi) const;

    void add(const message_info& mi);

    void start_piece();

    std::chrono::microseconds window_for(std::chrono::microseconds t) const;

private:
    limits p_limits;
    uint64_t p_messages{0};
    uint64_t p_lines{0};
    uint64_t p_bytes{0};
    uint64_t p_entries{0};
    std::optional<std::chrono::microseconds> p_window;
};

/**
 * The path of a piece of the given input.  A compression extension is
 * dropped since the pieces are written uncompressed, and the number goes
 * before an alphabetic extension so that "access.log.gz" becomes
 * "access.0001.log".
 */
std::filesystem::path piece_path(const std::filesystem::path& dir,
                                 const std::filesystem::path& input,
                                 size_t index);

/**
 * @return True if the file name looks like a piece of the given input.
 */
bool is_piece_name(const std::filesystem::path& filename,
                   const std::filesystem::path& input);

struct piece_summary {
    std::filesystem::path ps_path;
    uint64_t ps_lines{0};
    file_ssize_t ps_bytes{0};
    std::optional<std::chrono::microseconds> ps_first_time;
    std::optional<std::chrono::microseconds> ps_last_time;
};

struct options {
    std::filesystem::path o_output_dir{"."};
    limits o_limits;
    /** Only write the messages in this range. */
    std::optional<time_range> o_time_range;
};

struct summary {
    limits s_limits;
    std::vector<piece_summary> s_pieces;
    uint64_t s_lines{0};
    file_ssize_t s_bytes{0};
    /**
     * True if the timestamps leave out part of the date and the modification
     * times of the pieces were set to the times of their last messages, which
     * is where lnav gets the missing parts from.
     */
    bool s_mtimes_set{false};
};

using progress_cb = std::function<void(file_off_t)>;

/**
 * Split a file into pieces written to the output directory.  Nothing is
 * written when the file fits in a single piece.
 */
Result<summary, lnav::console::user_message> split(
    const std::filesystem::path& path,
    const options& opts,
    const progress_cb& on_progress = {});

}  // namespace lnav::file_split

#endif
