/**
 * Copyright (c) 2022, Timothy Stack
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

#ifndef lnav_indexing_hh
#define lnav_indexing_hh

#include "file_collection.hh"
#include "logfile_fwd.hh"

void rebuild_hist();

struct rebuild_indexes_result_t {
    size_t rir_changes{0};
    bool rir_completed{true};
    bool rir_rescan_needed{false};
};

rebuild_indexes_result_t rebuild_indexes(
    std::optional<ui_clock::time_point> deadline = std::nullopt);
void rebuild_indexes_repeatedly();
bool rescan_files(bool required = false);
bool update_active_files(file_collection& new_files);
lnav::progress_result_t do_observer_update(const logfile* lf);

/**
 * The UI tick for a parallel indexing pass.  Called on the UI thread while
 * workers run, with the offsets summed over the files in flight and a
 * per-file breakdown of the same reading in `in_flight`.
 *
 * It drives the loading bar and the file list, but every number it draws for
 * a file that is still in flight comes out of `in_flight`: the workers are
 * writing the size, the stat and the decompress error of those very files, so
 * nothing here may read them.  It publishes `in_flight` to
 * files_sub_source::fss_index_progress, which is what makes that possible and
 * what tells the rest of the UI a pass is running -- see the guards in
 * files_sub_source::text_selection_changed() and refresh_status_bars.
 */
lnav::progress_result_t indexing_scan_progress(
    file_off_t off,
    file_ssize_t total,
    const std::vector<index_progress_report>& in_flight);

#endif
