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

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>

#include "file_split.hh"

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "archive_manager.cfg.hh"
#include "base/fs_util.hh"
#include "base/humanize.hh"
#include "base/injector.hh"
#include "base/lnav_log.hh"
#include "base/parallel_for.hh"
#include "config.h"
#include "fmt/format.h"
#include "line_buffer.hh"
#include "log_format.hh"
#include "logfile.cfg.hh"
#include "logfile.hh"

namespace lnav::file_split {

static constexpr uint64_t MIN_DEFAULT_BYTES = 256ULL * 1024 * 1024;
static constexpr uint64_t MAX_DEFAULT_BYTES = 2ULL * 1024 * 1024 * 1024;
static constexpr uint64_t UNKNOWN_SIZE_DEFAULT_BYTES
    = 1ULL * 1024 * 1024 * 1024;
static constexpr file_ssize_t MAX_PREAMBLE_BYTES = 64 * 1024;
static constexpr size_t WRITE_BUFFER_SIZE = 1024 * 1024;
static constexpr file_ssize_t SPACE_CHECK_INTERVAL = 10 * 1024 * 1024;
static constexpr uint32_t FULL_DATE_FLAGS
    = ETF_YEAR_SET | ETF_MONTH_SET | ETF_DAY_SET;

limits
limits::defaults(std::optional<file_ssize_t> total_bytes,
                 size_t workers,
                 uint64_t max_lines)
{
    limits retval;

    // An eighth of the line limit leaves room for the extra lines JSON
    // messages are broken into, and a full set of workers indexing pieces at
    // once takes about as much memory as a single file at the limit.
    retval.l_max_entries = std::max<uint64_t>(1, max_lines / 8);
    if (total_bytes && total_bytes.value() > 0) {
        retval.l_bytes = std::clamp<uint64_t>(
            total_bytes.value() / std::max<size_t>(1, workers),
            MIN_DEFAULT_BYTES,
            MAX_DEFAULT_BYTES);
    } else {
        retval.l_bytes = UNKNOWN_SIZE_DEFAULT_BYTES;
    }

    return retval;
}

std::chrono::microseconds
policy::window_for(std::chrono::microseconds t) const
{
    const auto len = this->p_limits.l_duration.value().count();
    auto count = t.count() / len;
    if (t.count() % len < 0) {
        count -= 1;
    }

    return std::chrono::microseconds{count * len};
}

bool
policy::should_cut_before(const message_info& mi) const
{
    const auto& lim = this->p_limits;

    if (this->p_messages == 0) {
        return false;
    }
    if (lim.l_lines && this->p_lines + mi.mi_lines > lim.l_lines.value()) {
        return true;
    }
    if (lim.l_bytes
        && this->p_bytes + static_cast<uint64_t>(mi.mi_bytes)
            > lim.l_bytes.value())
    {
        return true;
    }
    if (lim.l_max_entries > 0
        && this->p_entries + mi.mi_entries > lim.l_max_entries)
    {
        return true;
    }
    if (lim.l_duration && mi.mi_time && this->p_window) {
        return this->window_for(mi.mi_time.value()) > this->p_window.value();
    }

    return false;
}

void
policy::add(const message_info& mi)
{
    this->p_messages += 1;
    this->p_lines += mi.mi_lines;
    this->p_bytes += mi.mi_bytes;
    this->p_entries += mi.mi_entries;
    if (this->p_limits.l_duration && mi.mi_time) {
        // A message that is earlier than the window stays in the current
        // piece rather than going back in time.
        auto win = this->window_for(mi.mi_time.value());
        if (!this->p_window || win > this->p_window.value()) {
            this->p_window = win;
        }
    }
}

void
policy::start_piece()
{
    this->p_messages = 0;
    this->p_lines = 0;
    this->p_bytes = 0;
    this->p_entries = 0;
    this->p_window = std::nullopt;
}

static const std::set<std::string> COMPRESSION_EXTENSIONS = {
    ".gz",
    ".bz2",
    ".xz",
    ".zst",
    ".lz4",
    ".Z",
};

struct piece_name_parts {
    std::string pnp_prefix;
    std::string pnp_suffix;
};

static piece_name_parts
name_parts_for(const std::filesystem::path& input)
{
    auto name = input.filename();

    if (COMPRESSION_EXTENSIONS.count(name.extension().string()) > 0) {
        name = name.stem();
    }

    const auto ext = name.extension().string();
    const auto alpha_ext = ext.size() > 1
        && std::all_of(ext.begin() + 1, ext.end(), [](unsigned char ch) {
                               return std::isalpha(ch);
                           });
    if (alpha_ext && !name.stem().empty()) {
        return {name.stem().string() + ".", ext};
    }

    return {name.string() + ".", ""};
}

std::filesystem::path
piece_path(const std::filesystem::path& dir,
           const std::filesystem::path& input,
           size_t index)
{
    const auto parts = name_parts_for(input);

    return dir
        / fmt::format(FMT_STRING("{}{:04}{}"),
                      parts.pnp_prefix,
                      index,
                      parts.pnp_suffix);
}

bool
is_piece_name(const std::filesystem::path& filename,
              const std::filesystem::path& input)
{
    const auto parts = name_parts_for(input);
    const auto name = filename.filename().string();

    if (name.size() <= parts.pnp_prefix.size() + parts.pnp_suffix.size()
        || name.compare(0, parts.pnp_prefix.size(), parts.pnp_prefix) != 0
        || name.compare(name.size() - parts.pnp_suffix.size(),
                        parts.pnp_suffix.size(),
                        parts.pnp_suffix)
            != 0)
    {
        return false;
    }

    return std::all_of(name.begin() + parts.pnp_prefix.size(),
                       name.end() - parts.pnp_suffix.size(),
                       [](unsigned char ch) { return std::isdigit(ch); });
}

namespace {

std::optional<uintmax_t>
available_space(const std::filesystem::path& dir)
{
    std::error_code ec;
    const auto info = std::filesystem::space(dir, ec);

    if (ec) {
        log_warning("unable to get the free space for %s -- %s",
                    dir.c_str(),
                    ec.message().c_str());
        return std::nullopt;
    }

    return info.available;
}

/**
 * Copies the original bytes of the input to the pieces.  The index trims line
 * endings and strips escape sequences from what it reads, so the bytes come
 * from a line_buffer of their own, which also decompresses the input the same
 * way the index does and so agrees with it on offsets.
 */
class piece_copier {
public:
    piece_copier(auto_fd& fd,
                 std::filesystem::path space_dir,
                 uint64_t min_free)
        : pc_space_dir(std::move(space_dir)), pc_min_free(min_free)
    {
        this->pc_buffer.set_fd(fd);
    }

    const line_buffer& get_buffer() const { return this->pc_buffer; }

    /**
     * Start the next copy at the given offset, which has to be the start of
     * a line.
     */
    void skip_to(file_off_t off) { this->pc_prev = file_range{off, 0}; }

    Result<std::string, std::string> read_prefix(file_off_t end)
    {
        std::string retval;
        auto prev = file_range{};

        while (prev.next_offset() < end) {
            auto load_res = this->pc_buffer.load_next_line(prev);
            if (load_res.isErr()) {
                return Err(load_res.unwrapErr());
            }
            auto li = load_res.unwrap();
            if (li.li_file_range.empty()) {
                break;
            }
            auto read_res = this->pc_buffer.read_range(li.li_file_range);
            if (read_res.isErr()) {
                return Err(read_res.unwrapErr());
            }
            auto sbr = read_res.unwrap();
            retval.append(sbr.get_data(), sbr.length());
            prev = li.li_file_range;
        }

        return Ok(std::move(retval));
    }

    /**
     * Copy the input from where the last copy stopped up to the given offset,
     * or to the end of the input.
     */
    Result<file_ssize_t, std::string> copy_to(int out_fd,
                                              std::optional<file_off_t> end)
    {
        file_ssize_t retval = 0;

        while (!end || this->pc_prev.next_offset() < end.value()) {
            auto load_res = this->pc_buffer.load_next_line(this->pc_prev);
            if (load_res.isErr()) {
                return Err(load_res.unwrapErr());
            }
            auto li = load_res.unwrap();
            if (li.li_file_range.empty()) {
                break;
            }
            if (end && li.li_file_range.next_offset() > end.value()) {
                return Err(std::string("input changed while splitting"));
            }
            auto read_res = this->pc_buffer.read_range(li.li_file_range);
            if (read_res.isErr()) {
                return Err(read_res.unwrapErr());
            }
            auto sbr = read_res.unwrap();
            this->pc_out.append(sbr.get_data(), sbr.length());
            retval += sbr.length();
            this->pc_prev = li.li_file_range;
            // Something else can be filling the disk too, so this is checked
            // along the way and not just before the first piece.
            this->pc_since_space_check += sbr.length();
            if (this->pc_since_space_check >= SPACE_CHECK_INTERVAL) {
                this->pc_since_space_check = 0;
                const auto avail = available_space(this->pc_space_dir);
                if (avail && avail.value() < this->pc_min_free) {
                    return Err(fmt::format(
                        FMT_STRING("available space in the output directory "
                                   "({}) is below the minimum-free threshold "
                                   "({})"),
                        humanize::file_size(avail.value(),
                                            humanize::alignment::none),
                        humanize::file_size(this->pc_min_free,
                                            humanize::alignment::none)));
                }
            }
            if (this->pc_out.size() >= WRITE_BUFFER_SIZE) {
                auto flush_res = this->flush(out_fd);
                if (flush_res.isErr()) {
                    return Err(flush_res.unwrapErr());
                }
            }
        }
        if (end && this->pc_prev.next_offset() != end.value()) {
            return Err(std::string("input changed while splitting"));
        }

        auto flush_res = this->flush(out_fd);
        if (flush_res.isErr()) {
            return Err(flush_res.unwrapErr());
        }

        return Ok(retval);
    }

    static Result<void, std::string> write_all(int out_fd,
                                               const char* data,
                                               size_t len)
    {
        while (len > 0) {
            auto rc = write(out_fd, data, len);
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return Err(std::string(strerror(errno)));
            }
            data += rc;
            len -= rc;
        }

        return Ok();
    }

private:
    Result<void, std::string> flush(int out_fd)
    {
        auto rc = write_all(out_fd, this->pc_out.data(), this->pc_out.size());
        this->pc_out.clear();

        return rc;
    }

    line_buffer pc_buffer;
    file_range pc_prev;
    std::string pc_out;
    std::filesystem::path pc_space_dir;
    uint64_t pc_min_free;
    file_ssize_t pc_since_space_check{0};
};

bool
starts_message(const logline& ll)
{
    return !ll.is_continued() && ll.get_sub_offset() == 0;
}

}  // namespace

Result<summary, lnav::console::user_message>
split(const std::filesystem::path& path,
      const options& opts,
      const progress_cb& on_progress)
{
    auto stat_res = lnav::filesystem::stat_file(path);
    if (stat_res.isErr()) {
        return Err(lnav::console::user_message::error(
                       attr_line_t("unable to split file: ")
                           .append(lnav::roles::file(path.string())))
                       .with_reason(stat_res.unwrapErr()));
    }
    const auto st = stat_res.unwrap();
    if (!S_ISREG(st.st_mode)) {
        return Err(lnav::console::user_message::error(
                       attr_line_t("unable to split file: ")
                           .append(lnav::roles::file(path.string())))
                       .with_reason("not a regular file"));
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(opts.o_output_dir, ec)) {
        return Err(
            lnav::console::user_message::error(
                attr_line_t("invalid output directory: ")
                    .append(lnav::roles::file(opts.o_output_dir.string())))
                .with_reason("not a directory"));
    }
    std::optional<std::filesystem::path> existing_piece;
    for (const auto& entry :
         std::filesystem::directory_iterator(opts.o_output_dir, ec))
    {
        if (is_piece_name(entry.path(), path)
            && (!existing_piece || entry.path() < existing_piece.value()))
        {
            existing_piece = entry.path();
        }
    }
    if (existing_piece) {
        return Err(lnav::console::user_message::error(
                       attr_line_t("refusing to overwrite pieces of ")
                           .append(lnav::roles::file(path.string())))
                       .with_reason(attr_line_t("found existing file ")
                                        .append(lnav::roles::file(
                                            existing_piece->string())))
                       .with_help("remove the existing pieces or use a "
                                  "different output directory"));
    }

    const auto& lf_cfg = injector::get<const lnav::logfile::config&>();
    const auto max_lines = std::min(::logfile::MAX_LINES, lf_cfg.lc_max_lines);

    auto loo = logfile_open_options{};
    loo.with_streaming(true).with_follow(false).with_include_in_session(false);

    // Formats like CSV and W3C need their header lines to be recognized, so
    // the lines lnav ignores at the start of the file are repeated at the top
    // of the pieces that do not already start with them.  A time range makes
    // the index skip ahead before those lines can be seen, so they are found
    // with a short read of their own.
    std::optional<file_off_t> preamble_end;
    {
        auto head_loo = loo;
        head_loo.loo_stream_batch_lines = ::logfile::RETRY_MATCH_SIZE;
        auto head_res = ::logfile::open(path, head_loo);
        if (head_res.isErr()) {
            return Err(lnav::console::user_message::error(
                           attr_line_t("unable to open file: ")
                               .append(lnav::roles::file(path.string())))
                           .with_reason(head_res.unwrapErr()));
        }
        auto head_lf = head_res.unwrap();
        // Indexing stops as soon as a format is found, which can be before
        // any line past the header has been read.
        while (head_lf->size() < ::logfile::RETRY_MATCH_SIZE) {
            const auto rr = head_lf->rebuild_index();
            if (rr == ::logfile::rebuild_result_t::NO_NEW_LINES
                || rr == ::logfile::rebuild_result_t::INVALID)
            {
                break;
            }
        }
        if (head_lf->get_format() != nullptr) {
            size_t first_msg = 0;
            while (first_msg < head_lf->size()
                   && (*head_lf)[first_msg].is_ignored())
            {
                first_msg += 1;
            }
            if (first_msg > 0 && first_msg < head_lf->size()) {
                const auto end = (*head_lf)[first_msg].get_offset();
                if (end <= MAX_PREAMBLE_BYTES) {
                    preamble_end = end;
                }
            }
        }
    }

    if (opts.o_time_range) {
        loo.with_time_range(opts.o_time_range.value());
    }
    auto open_res = ::logfile::open(path, loo);
    if (open_res.isErr()) {
        return Err(lnav::console::user_message::error(
                       attr_line_t("unable to open file: ")
                           .append(lnav::roles::file(path.string())))
                       .with_reason(open_res.unwrapErr()));
    }
    auto lf = open_res.unwrap();

    auto copier_fd_res = lnav::filesystem::open_file(path, O_RDONLY);
    if (copier_fd_res.isErr()) {
        return Err(lnav::console::user_message::error(
                       attr_line_t("unable to open file: ")
                           .append(lnav::roles::file(path.string())))
                       .with_reason(copier_fd_res.unwrapErr()));
    }
    auto copier_fd = copier_fd_res.unwrap();
    const auto min_free
        = injector::get<const ::archive_manager::config&>().amc_min_free_space;
    piece_copier copier(copier_fd, opts.o_output_dir, min_free);

    std::string preamble;
    if (preamble_end) {
        auto pre_res = copier.read_prefix(preamble_end.value());
        if (pre_res.isOk()) {
            preamble = pre_res.unwrap();
        }
    }

    summary retval;
    std::optional<policy> pol;
    piece_summary curr_piece;
    // Where the piece being built starts in the input.
    std::optional<file_off_t> curr_start_off;
    // Whether the copy has been moved to the first message that is written.
    auto copy_positioned = false;
    uint64_t timed_messages = 0;
    // Where the first message past the end of the time range starts.
    std::optional<file_off_t> range_end_off;
    // Index of the first entry not yet assigned to a piece, counted from the
    // start of the file rather than from what is still in the index.
    size_t next_index = 0;

    auto fail = [&retval](lnav::console::user_message um) {
        for (const auto& piece : retval.s_pieces) {
            std::error_code rm_ec;
            std::filesystem::remove(piece.ps_path, rm_ec);
        }
        return um;
    };

    auto write_piece = [&](std::optional<file_off_t> end)
        -> Result<void, lnav::console::user_message> {
        // Checked when the first piece is about to be written rather than up
        // front, since a file that fits in a single piece writes nothing.
        if (retval.s_pieces.empty()) {
            const auto avail = available_space(opts.o_output_dir);
            // Without a time range, all of the input ends up in the pieces.
            // Compressed input only has a hint of its size, which cannot be
            // less than what has been read already.
            std::optional<uint64_t> needed;
            if (!opts.o_time_range) {
                if (!lf->is_compressed()) {
                    needed = st.st_size;
                } else {
                    const auto hint = copier.get_buffer().uncompressed_size();
                    if (hint && hint.value() >= lf->get_index_size()) {
                        needed = hint.value();
                    }
                }
            }
            if (avail && avail.value() < needed.value_or(0) + min_free) {
                auto reason = attr_line_t();
                if (needed) {
                    reason.append("the pieces need about ")
                        .append(lnav::roles::number(humanize::file_size(
                            needed.value(), humanize::alignment::none)))
                        .append(" and ");
                }
                reason
                    .append(lnav::roles::number(humanize::file_size(
                        min_free, humanize::alignment::none)))
                    .append(" must stay free, but only ")
                    .append(lnav::roles::number(humanize::file_size(
                        avail.value(), humanize::alignment::none)))
                    .append(" is available");
                return Err(
                    lnav::console::user_message::error(
                        attr_line_t("not enough free space in ")
                            .append(
                                lnav::roles::file(opts.o_output_dir.string()))
                            .append(" to split ")
                            .append(lnav::roles::file(path.string())))
                        .with_reason(reason)
                        .with_help("free up space, write the pieces to another "
                                   "directory with -o, or lower "
                                   "/tuning/archive-manager/min-free-space"));
            }
        }

        curr_piece.ps_path
            = piece_path(opts.o_output_dir, path, retval.s_pieces.size() + 1);
        auto create_res = lnav::filesystem::create_file(
            curr_piece.ps_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (create_res.isErr()) {
            return Err(
                lnav::console::user_message::error(
                    attr_line_t("unable to create file: ")
                        .append(lnav::roles::file(curr_piece.ps_path.string())))
                    .with_reason(create_res.unwrapErr()));
        }
        auto out_fd = create_res.unwrap();
        retval.s_pieces.emplace_back(curr_piece);

        auto write_err = [&](const std::string& msg) {
            return lnav::console::user_message::error(
                       attr_line_t("unable to write file: ")
                           .append(
                               lnav::roles::file(curr_piece.ps_path.string())))
                .with_reason(msg);
        };
        if (!preamble.empty()
            && curr_start_off.value_or(0) >= preamble_end.value_or(0))
        {
            auto pre_res = piece_copier::write_all(
                out_fd.get(), preamble.data(), preamble.size());
            if (pre_res.isErr()) {
                return Err(write_err(pre_res.unwrapErr()));
            }
        }
        auto copy_res = copier.copy_to(out_fd.get(), end);
        if (copy_res.isErr()) {
            return Err(write_err(copy_res.unwrapErr()));
        }
        retval.s_pieces.back().ps_bytes = copy_res.unwrap();

        curr_piece = piece_summary{};
        curr_start_off = std::nullopt;
        return Ok();
    };

    auto eof = false;
    while (!eof) {
        const auto rr = lf->rebuild_index();
        if (rr == ::logfile::rebuild_result_t::INVALID || lf->is_closed()) {
            return Err(fail(lnav::console::user_message::error(
                                attr_line_t("unable to read file: ")
                                    .append(lnav::roles::file(path.string())))
                                .with_reason("the file could not be indexed")));
        }
        if (lf->get_text_format() == text_format_t::TF_BINARY) {
            return Err(fail(lnav::console::user_message::error(
                                attr_line_t("unable to split file: ")
                                    .append(lnav::roles::file(path.string())))
                                .with_reason("the file is binary")));
        }
        if (!lf->is_indexing()) {
            auto um = lnav::console::user_message::error(
                attr_line_t("unable to split file: ")
                    .append(lnav::roles::file(path.string())));
            const auto notes = lf->get_notes();
            for (const auto& [kind, note] : notes.entries()) {
                um.with_note(note.to_attr_line());
            }
            return Err(fail(um));
        }
        eof = rr == ::logfile::rebuild_result_t::NO_NEW_LINES;
        if (on_progress) {
            on_progress(lf->get_index_size());
        }

        const auto base = lf->get_index_base();
        const auto size = lf->size();
        if (base + size < next_index) {
            // A better format was found and the file was indexed again.
            if (!retval.s_pieces.empty()) {
                return Err(
                    fail(lnav::console::user_message::error(
                             attr_line_t("unable to split file: ")
                                 .append(lnav::roles::file(path.string())))
                             .with_reason("the log format changed while "
                                          "splitting")));
            }
            next_index = 0;
            pol = std::nullopt;
            curr_piece = piece_summary{};
            curr_start_off = std::nullopt;
            copy_positioned = false;
            timed_messages = 0;
            retval.s_lines = 0;
            retval.s_bytes = 0;
        }
        // Timestamps that leave out part of the date are moved back when the
        // date is seen to roll over, which includes the messages that are
        // already in a piece.
        for (const auto& rollover : lf->get_time_rollovers()) {
            auto shift = [&rollover](piece_summary& ps) {
                if (ps.ps_first_time) {
                    ps.ps_first_time = rollover.apply(ps.ps_first_time.value());
                }
                if (ps.ps_last_time) {
                    ps.ps_last_time = rollover.apply(ps.ps_last_time.value());
                }
            };
            for (auto& piece : retval.s_pieces) {
                shift(piece);
            }
            shift(curr_piece);
        }

        if (!eof && base + size < ::logfile::RETRY_MATCH_SIZE) {
            continue;
        }

        if (!pol) {
            limits lim;
            if (opts.o_limits.has_criteria()) {
                lim = opts.o_limits;
                lim.l_max_entries = max_lines;
            } else {
                // The size of compressed content is not known until it has
                // all been read.
                lim = limits::defaults(lf->is_compressed()
                                           ? std::nullopt
                                           : std::make_optional(st.st_size),
                                       lnav::default_worker_count(SIZE_MAX),
                                       max_lines);
            }
            if (lim.l_duration || opts.o_time_range) {
                if (lf->get_format() == nullptr) {
                    return Err(
                        lnav::console::user_message::error(
                            attr_line_t("unable to split file by time: ")
                                .append(lnav::roles::file(path.string())))
                            .with_reason("no log format with timestamps was "
                                         "detected"));
                }
                // The missing parts are filled in from the file's
                // modification time and can be corrected once more of the
                // file has been read, which is too late to decide where a
                // message goes.
                const auto ts_flags = lf->get_format()->lf_timestamp_flags;
                if ((ts_flags & FULL_DATE_FLAGS) != FULL_DATE_FLAGS) {
                    std::vector<std::string> missing;
                    if (!(ts_flags & ETF_YEAR_SET)) {
                        missing.emplace_back("year");
                    }
                    if (!(ts_flags & ETF_MONTH_SET)) {
                        missing.emplace_back("month");
                    }
                    if (!(ts_flags & ETF_DAY_SET)) {
                        missing.emplace_back("day");
                    }
                    auto missing_str = missing.back();
                    if (missing.size() == 2) {
                        missing_str = fmt::format(
                            FMT_STRING("{} and {}"), missing[0], missing[1]);
                    } else if (missing.size() == 3) {
                        missing_str = fmt::format(FMT_STRING("{}, {}, and {}"),
                                                  missing[0],
                                                  missing[1],
                                                  missing[2]);
                    }
                    return Err(
                        lnav::console::user_message::error(
                            attr_line_t("unable to split file by time: ")
                                .append(lnav::roles::file(path.string())))
                            .with_reason(fmt::format(
                                FMT_STRING("the timestamps in the file do not "
                                           "include the {}"),
                                missing_str))
                            .with_help("split the file with --lines or --size "
                                       "instead"));
                }
            }
            pol.emplace(lim);
            retval.s_limits = lim;
        }

        auto local = next_index - base;
        auto frontier = size;
        if (!eof) {
            // The last message can still grow or be rolled back by the next
            // rebuild, so it waits for the next round.
            frontier = size > 0 ? size - 1 : 0;
            while (frontier > local && !starts_message((*lf)[frontier])) {
                frontier -= 1;
            }
        }

        while (local < frontier) {
            auto msg_end = local + 1;
            while (msg_end < size && !starts_message((*lf)[msg_end])) {
                msg_end += 1;
            }

            const auto& first_ll = (*lf)[local];
            const auto start_off = first_ll.get_offset();
            // The index size counts the line that ended a time range, which
            // is not part of the last message.
            const auto end_off = msg_end < size
                ? (*lf)[msg_end].get_offset()
                : static_cast<file_off_t>(lf->get_upper_bound_offset().value_or(
                      lf->get_index_size()));
            message_info mi;
            mi.mi_bytes = end_off - start_off;
            mi.mi_entries = msg_end - local;
            for (auto lpc = local; lpc < msg_end; lpc++) {
                if ((*lf)[lpc].get_sub_offset() == 0) {
                    mi.mi_lines += 1;
                }
            }
            if (lf->get_format() != nullptr && !first_ll.is_ignored()) {
                mi.mi_time = first_ll.get_time<std::chrono::microseconds>();
            }

            // The index can still hold messages just outside of the time
            // range, so they are checked here as well.
            if (opts.o_time_range) {
                const auto& range = opts.o_time_range.value();
                if (mi.mi_time && mi.mi_time.value() > range.tr_end) {
                    range_end_off = start_off;
                    break;
                }
                // Ignored lines, like a CSV header, have no time of their own,
                // so they are skipped until the first message in the range is
                // found.  The preamble puts the header back at the top of the
                // piece.
                if (!copy_positioned && range.has_lower_bound()
                    && (!mi.mi_time || mi.mi_time.value() < range.tr_begin))
                {
                    local = msg_end;
                    continue;
                }
            }

            if (pol->should_cut_before(mi)) {
                auto write_res = write_piece(start_off);
                if (write_res.isErr()) {
                    return Err(fail(write_res.unwrapErr()));
                }
                pol->start_piece();
            }

            if (!copy_positioned) {
                // With a time range, the first message is not at the start of
                // the file.
                copier.skip_to(start_off);
                copy_positioned = true;
            }
            if (!curr_start_off) {
                curr_start_off = start_off;
            }
            if (mi.mi_time) {
                timed_messages += 1;
            }
            pol->add(mi);
            curr_piece.ps_lines += mi.mi_lines;
            retval.s_lines += mi.mi_lines;
            retval.s_bytes += mi.mi_bytes;
            if (mi.mi_time) {
                if (!curr_piece.ps_first_time) {
                    curr_piece.ps_first_time = mi.mi_time;
                }
                curr_piece.ps_last_time = mi.mi_time;
            }
            local = msg_end;
        }

        next_index = base + local;
        lf->discard_index_before(local);
        if (range_end_off) {
            break;
        }
    }

    // A time range asks for the messages in it, so they are written even when
    // they fit in a single piece.
    if (!retval.s_pieces.empty() || (opts.o_time_range && timed_messages > 0)) {
        auto end = range_end_off;
        if (!end && lf->get_upper_bound_offset()) {
            end = static_cast<file_off_t>(lf->get_upper_bound_offset().value());
        }
        auto write_res = write_piece(end);
        if (write_res.isErr()) {
            return Err(fail(write_res.unwrapErr()));
        }
    }

    // lnav fills in a missing year from the file's modification time, so
    // each piece gets the time of its last message.  This is done at the end
    // since those times can be corrected until the whole file has been read.
    const auto partial_dates = lf->get_format() != nullptr
        && (lf->get_format()->lf_timestamp_flags & FULL_DATE_FLAGS)
            != FULL_DATE_FLAGS;
    for (const auto& piece : retval.s_pieces) {
        if (!piece.ps_last_time) {
            continue;
        }
        const auto secs = std::chrono::duration_cast<std::chrono::seconds>(
            piece.ps_last_time.value());
        timeval tv[2];
        tv[0].tv_sec = tv[1].tv_sec = secs.count();
        tv[0].tv_usec = tv[1].tv_usec = 0;
        if (utimes(piece.ps_path.c_str(), tv) == -1) {
            log_warning("unable to set the time of %s -- %s",
                        piece.ps_path.c_str(),
                        strerror(errno));
        } else if (partial_dates) {
            retval.s_mtimes_set = true;
        }
    }

    return Ok(std::move(retval));
}

}  // namespace lnav::file_split
