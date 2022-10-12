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
 * @file file_collection.cc
 */

#include <unordered_map>

#include "file_collection.hh"

#include <glob.h>

#include "base/humanize.network.hh"
#include "base/isc.hh"
#include "base/itertools.hh"
#include "base/opt_util.hh"
#include "base/string_util.hh"
#include "config.h"
#include "lnav_util.hh"
#include "logfile.hh"
#include "pcap_manager.hh"
#include "service_tags.hh"
#include "tailer/tailer.looper.hh"

static std::mutex REALPATH_CACHE_MUTEX;
static std::unordered_map<std::string, std::string> REALPATH_CACHE;

child_poll_result_t
child_poller::poll(file_collection& fc)
{
    if (!this->cp_child) {
        return child_poll_result_t::FINISHED;
    }

    auto poll_res = std::move(this->cp_child.value()).poll();
    this->cp_child = nonstd::nullopt;
    return poll_res.match(
        [this](auto_pid<process_state::running>& alive) {
            this->cp_child = std::move(alive);
            return child_poll_result_t::ALIVE;
        },
        [this, &fc](auto_pid<process_state::finished>& finished) {
            require(this->cp_finalizer);

            this->cp_finalizer(fc, finished);
            return child_poll_result_t::FINISHED;
        });
}

void
file_collection::close_files(const std::vector<std::shared_ptr<logfile>>& files)
{
    for (const auto& lf : files) {
        auto actual_path_opt = lf->get_actual_path();

        if (actual_path_opt) {
            std::lock_guard<std::mutex> lg(REALPATH_CACHE_MUTEX);
            auto path_str = actual_path_opt.value().string();

            for (auto iter = REALPATH_CACHE.begin();
                 iter != REALPATH_CACHE.end();)
            {
                if (iter->first == path_str || iter->second == path_str) {
                    iter = REALPATH_CACHE.erase(iter);
                } else {
                    ++iter;
                }
            }
        } else {
            this->fc_file_names.erase(lf->get_filename());
        }
        auto file_iter = find(this->fc_files.begin(), this->fc_files.end(), lf);
        if (file_iter != this->fc_files.end()) {
            this->fc_files.erase(file_iter);
        }
    }
    this->fc_files_generation += 1;

    this->regenerate_unique_file_names();
}

void
file_collection::regenerate_unique_file_names()
{
    unique_path_generator upg;

    for (const auto& lf : this->fc_files) {
        upg.add_source(lf);
    }

    upg.generate();

    this->fc_largest_path_length = 0;
    for (const auto& pair : this->fc_name_to_errors) {
        auto path = ghc::filesystem::path(pair.first).filename().string();

        if (path.length() > this->fc_largest_path_length) {
            this->fc_largest_path_length = path.length();
        }
    }
    for (const auto& lf : this->fc_files) {
        const auto& path = lf->get_unique_path();

        if (path.length() > this->fc_largest_path_length) {
            this->fc_largest_path_length = path.length();
        }
    }
    for (const auto& pair : this->fc_other_files) {
        switch (pair.second.ofd_format) {
            case file_format_t::UNKNOWN:
            case file_format_t::ARCHIVE:
            case file_format_t::PCAP:
            case file_format_t::SQLITE_DB: {
                auto bn = ghc::filesystem::path(pair.first).filename().string();
                if (bn.length() > this->fc_largest_path_length) {
                    this->fc_largest_path_length = bn.length();
                }
                break;
            }
            case file_format_t::REMOTE: {
                if (pair.first.length() > this->fc_largest_path_length) {
                    this->fc_largest_path_length = pair.first.length();
                }
                break;
            }
        }
    }
}

void
file_collection::merge(file_collection& other)
{
    this->fc_recursive = this->fc_recursive || other.fc_recursive;
    this->fc_rotated = this->fc_rotated || other.fc_rotated;

    this->fc_synced_files.insert(other.fc_synced_files.begin(),
                                 other.fc_synced_files.end());
    this->fc_name_to_errors.insert(other.fc_name_to_errors.begin(),
                                   other.fc_name_to_errors.end());
    this->fc_file_names.insert(
        std::make_move_iterator(other.fc_file_names.begin()),
        std::make_move_iterator(other.fc_file_names.end()));
    if (!other.fc_files.empty()) {
        for (const auto& lf : other.fc_files) {
            this->fc_name_to_errors.erase(lf->get_filename());
        }
        this->fc_files.insert(
            this->fc_files.end(), other.fc_files.begin(), other.fc_files.end());
        this->fc_files_generation += 1;
    }
    for (auto& pair : other.fc_renamed_files) {
        pair.first->set_filename(pair.second);
    }
    this->fc_closed_files.insert(other.fc_closed_files.begin(),
                                 other.fc_closed_files.end());
    this->fc_other_files.insert(other.fc_other_files.begin(),
                                other.fc_other_files.end());
    if (!other.fc_child_pollers.empty()) {
        this->fc_child_pollers.insert(
            this->fc_child_pollers.begin(),
            std::make_move_iterator(other.fc_child_pollers.begin()),
            std::make_move_iterator(other.fc_child_pollers.end()));
        other.fc_child_pollers.clear();
    }
}

/**
 * Functor used to compare files based on their device and inode number.
 */
struct same_file {
    explicit same_file(const struct stat& stat) : sf_stat(stat){};

    /**
     * Compare the given log file against the 'stat' given in the constructor.
     * @param  lf The log file to compare.
     * @return    True if the dev/inode values in the stat given in the
     *   constructor matches the stat in the logfile object.
     */
    bool operator()(const std::shared_ptr<logfile>& lf) const
    {
        return !lf->is_closed() && this->sf_stat.st_dev == lf->get_stat().st_dev
            && this->sf_stat.st_ino == lf->get_stat().st_ino;
    }

    const struct stat& sf_stat;
};

/**
 * Try to load the given file as a log file.  If the file has not already been
 * loaded, it will be loaded.  If the file has already been loaded, the file
 * name will be updated.
 *
 * @param filename The file name to check.
 * @param fd       An already-opened descriptor for 'filename'.
 * @param required Specifies whether or not the file must exist and be valid.
 */
std::future<file_collection>
file_collection::watch_logfile(const std::string& filename,
                               logfile_open_options& loo,
                               bool required)
{
    file_collection retval;
    struct stat st;
    int rc;

    if (this->fc_closed_files.count(filename)) {
        return lnav::futures::make_ready_future(std::move(retval));
    }

    if (loo.loo_fd != -1) {
        rc = fstat(loo.loo_fd, &st);
        if (rc == 0) {
            loo.with_stat_for_temp(st);
        }
    } else if (loo.loo_temp_file) {
        memset(&st, 0, sizeof(st));
        st.st_dev = loo.loo_temp_dev;
        st.st_ino = loo.loo_temp_ino;
        st.st_mode = S_IFREG;
        rc = 0;
    } else {
        rc = stat(filename.c_str(), &st);
    }

    if (rc == 0) {
        if (S_ISDIR(st.st_mode) && this->fc_recursive) {
            std::string wilddir = filename + "/*";

            if (this->fc_file_names.find(wilddir) == this->fc_file_names.end())
            {
                retval.fc_file_names.emplace(wilddir, logfile_open_options());
            }
            return lnav::futures::make_ready_future(std::move(retval));
        }
        if (!S_ISREG(st.st_mode)) {
            if (required) {
                rc = -1;
                errno = EINVAL;
            } else {
                return lnav::futures::make_ready_future(std::move(retval));
            }
        }
        auto err_iter = this->fc_name_to_errors.find(filename);
        if (err_iter != this->fc_name_to_errors.end()) {
            if (err_iter->second.fei_mtime != st.st_mtime) {
                this->fc_name_to_errors.erase(err_iter);
            }
        }
    }
    if (rc == -1) {
        if (required) {
            retval.fc_name_to_errors.emplace(filename,
                                             file_error_info{
                                                 time(nullptr),
                                                 std::string(strerror(errno)),
                                             });
        }
        return lnav::futures::make_ready_future(std::move(retval));
    }

    if (this->fc_new_stats | lnav::itertools::find_if([&st](const auto& elem) {
            return st.st_ino == elem.st_ino && st.st_dev == elem.st_dev;
        }))
    {
        // this file is probably a link that we have already scanned in this
        // pass.
        return lnav::futures::make_ready_future(std::move(retval));
    }

    this->fc_new_stats.emplace_back(st);

    auto file_iter = std::find_if(
        this->fc_files.begin(), this->fc_files.end(), same_file(st));

    if (file_iter == this->fc_files.end()) {
        if (this->fc_other_files.find(filename) != this->fc_other_files.end()) {
            return lnav::futures::make_ready_future(std::move(retval));
        }

        require(this->fc_progress.get() != nullptr);

        auto func = [filename,
                     st,
                     loo2 = std::move(loo),
                     prog = this->fc_progress,
                     errs = this->fc_name_to_errors]() mutable {
            file_collection retval;

            if (errs.find(filename) != errs.end()) {
                // The file is broken, no reason to try and reopen
                return retval;
            }

            auto ff = loo2.loo_temp_file ? file_format_t::UNKNOWN
                                         : detect_file_format(filename);

            loo2.loo_file_format = ff;
            switch (ff) {
                case file_format_t::SQLITE_DB:
                    retval.fc_other_files[filename].ofd_format = ff;
                    break;

                case file_format_t::PCAP: {
                    auto res = pcap_manager::convert(filename);

                    if (res.isOk()) {
                        auto convert_res = res.unwrap();

                        loo2.with_fd(std::move(convert_res.cr_destination));
                        retval.fc_child_pollers.emplace_back(child_poller{
                            std::move(convert_res.cr_child),
                            [filename,
                             st,
                             error_queue = convert_res.cr_error_queue](
                                auto& fc, auto& child) {
                                if (child.was_normal_exit()
                                    && child.exit_status() == EXIT_SUCCESS)
                                {
                                    log_info("pcap[%d] exited normally",
                                             child.in());
                                    return;
                                }
                                log_error("pcap[%d] exited with %d",
                                          child.in(),
                                          child.status());
                                fc.fc_name_to_errors.emplace(
                                    filename,
                                    file_error_info{
                                        st.st_mtime,
                                        fmt::format(
                                            FMT_STRING("{}"),
                                            fmt::join(*error_queue, "\n")),
                                    });
                            },
                        });
                        auto open_res = logfile::open(filename, loo2);
                        if (open_res.isOk()) {
                            retval.fc_files.push_back(open_res.unwrap());
                        } else {
                            retval.fc_name_to_errors.emplace(
                                filename,
                                file_error_info{
                                    st.st_mtime,
                                    open_res.unwrapErr(),
                                });
                        }
                    } else {
                        retval.fc_name_to_errors.emplace(filename,
                                                         file_error_info{
                                                             st.st_mtime,
                                                             res.unwrapErr(),
                                                         });
                    }
                    break;
                }

                case file_format_t::ARCHIVE: {
                    nonstd::optional<
                        std::list<archive_manager::extract_progress>::iterator>
                        prog_iter_opt;

                    if (loo2.loo_source == logfile_name_source::ARCHIVE) {
                        // Don't try to open nested archives
                        return retval;
                    }

                    auto res = archive_manager::walk_archive_files(
                        filename,
                        [prog, &prog_iter_opt](const auto& path,
                                               const auto total) {
                            safe::WriteAccess<safe_scan_progress> sp(*prog);

                            prog_iter_opt | [&sp](auto prog_iter) {
                                sp->sp_extractions.erase(prog_iter);
                            };
                            auto prog_iter = sp->sp_extractions.emplace(
                                sp->sp_extractions.begin(), path, total);
                            prog_iter_opt = prog_iter;

                            return &(*prog_iter);
                        },
                        [&filename, &retval](const auto& tmp_path,
                                             const auto& entry) {
                            auto arc_path = ghc::filesystem::relative(
                                entry.path(), tmp_path);
                            auto custom_name = filename / arc_path;
                            bool is_visible = true;

                            if (entry.file_size() == 0) {
                                log_info("hiding empty archive file: %s",
                                         entry.path().c_str());
                                is_visible = false;
                            }

                            log_info("adding file from archive: %s/%s",
                                     filename.c_str(),
                                     entry.path().c_str());
                            retval.fc_file_names[entry.path().string()]
                                .with_filename(custom_name.string())
                                .with_source(logfile_name_source::ARCHIVE)
                                .with_visibility(is_visible)
                                .with_non_utf_visibility(false)
                                .with_visible_size_limit(256 * 1024);
                        });
                    if (res.isErr()) {
                        log_error("archive extraction failed: %s",
                                  res.unwrapErr().c_str());
                        retval.clear();
                        retval.fc_name_to_errors.emplace(filename,
                                                         file_error_info{
                                                             st.st_mtime,
                                                             res.unwrapErr(),
                                                         });
                    } else {
                        retval.fc_other_files[filename] = ff;
                    }
                    {
                        prog_iter_opt | [&prog](auto prog_iter) {
                            prog->writeAccess()->sp_extractions.erase(
                                prog_iter);
                        };
                    }
                    break;
                }

                default:
                    log_info("loading new file: filename=%s", filename.c_str());

                    auto open_res = logfile::open(filename, loo2);
                    if (open_res.isOk()) {
                        retval.fc_files.push_back(open_res.unwrap());
                    } else {
                        retval.fc_name_to_errors.emplace(
                            filename,
                            file_error_info{
                                st.st_mtime,
                                open_res.unwrapErr(),
                            });
                    }
                    break;
            }

            return retval;
        };

        return std::async(std::launch::async, std::move(func));
    }

    auto lf = *file_iter;

    if (lf->is_valid_filename() && lf->get_filename() != filename) {
        /* The file is already loaded, but has been found under a different
         * name.  We just need to update the stored file name.
         */
        retval.fc_renamed_files.emplace_back(lf, filename);
    }

    return lnav::futures::make_ready_future(std::move(retval));
}

/**
 * Expand a glob pattern and call watch_logfile with the file names that match
 * the pattern.
 * @param path     The glob pattern to expand.
 * @param required Passed to watch_logfile.
 */
void
file_collection::expand_filename(
    lnav::futures::future_queue<file_collection>& fq,
    const std::string& path,
    logfile_open_options& loo,
    bool required)
{
    static_root_mem<glob_t, globfree> gl;

    {
        std::lock_guard<std::mutex> lg(REALPATH_CACHE_MUTEX);

        if (REALPATH_CACHE.find(path) != REALPATH_CACHE.end()) {
            return;
        }
    }

    if (is_url(path.c_str())) {
        return;
    }

    if (glob(path.c_str(), GLOB_NOCHECK, nullptr, gl.inout()) == 0) {
        int lpc;

        if (gl->gl_pathc == 1 /*&& gl.gl_matchc == 0*/) {
            /* It's a pattern that doesn't match any files
             * yet, allow it through since we'll load it in
             * dynamically.
             */
            if (access(gl->gl_pathv[0], F_OK) == -1) {
                auto rp_opt = humanize::network::path::from_str(path);
                if (rp_opt) {
                    auto iter = this->fc_other_files.find(path);
                    auto rp = *rp_opt;

                    if (iter != this->fc_other_files.end()) {
                        return;
                    }

                    file_collection retval;
                    logfile_open_options_base loo_base{loo};

                    isc::to<tailer::looper&, services::remote_tailer_t>().send(
                        [rp, loo_base](auto& tlooper) {
                            tlooper.add_remote(rp, loo_base);
                        });
                    retval.fc_other_files[path] = file_format_t::REMOTE;
                    {
                        this->fc_progress->writeAccess()
                            ->sp_tailers[fmt::to_string(rp.home())]
                            .tp_message
                            = "Initializing...";
                    }

                    fq.push_back(
                        lnav::futures::make_ready_future(std::move(retval)));
                    return;
                }

                required = false;
            }
        }
        if (gl->gl_pathc > 1 || strcmp(path.c_str(), gl->gl_pathv[0]) != 0) {
            required = false;
        }

        std::lock_guard<std::mutex> lg(REALPATH_CACHE_MUTEX);
        for (lpc = 0; lpc < (int) gl->gl_pathc; lpc++) {
            auto path_str = std::string(gl->gl_pathv[lpc]);
            auto iter = REALPATH_CACHE.find(path_str);

            if (iter == REALPATH_CACHE.end()) {
                auto_mem<char> abspath;

                if ((abspath = realpath(gl->gl_pathv[lpc], nullptr)) == nullptr)
                {
                    auto* errmsg = strerror(errno);

                    if (required) {
                        fprintf(stderr,
                                "Cannot find file: %s -- %s",
                                gl->gl_pathv[lpc],
                                errmsg);
                    } else if (loo.loo_source != logfile_name_source::REMOTE) {
                        // XXX The remote code path adds the file name before
                        // the file exists...  not sure checking for that here
                        // is a good idea (prolly not)
                        file_collection retval;

                        if (gl->gl_pathc == 1) {
                            retval.fc_name_to_errors.emplace(path,
                                                             file_error_info{
                                                                 time(nullptr),
                                                                 errmsg,
                                                             });
                        } else {
                            retval.fc_name_to_errors.emplace(path_str,
                                                             file_error_info{
                                                                 time(nullptr),
                                                                 errmsg,
                                                             });
                        }
                        fq.push_back(lnav::futures::make_ready_future(
                            std::move(retval)));
                    }
                    continue;
                }

                auto p = REALPATH_CACHE.emplace(path_str, abspath.in());

                iter = p.first;
            }

            if (required || access(iter->second.c_str(), R_OK) == 0) {
                fq.push_back(watch_logfile(iter->second, loo, required));
            }
        }
    }
}

file_collection
file_collection::rescan_files(bool required)
{
    file_collection retval;
    lnav::futures::future_queue<file_collection> fq(
        [&retval](auto& fc) { retval.merge(fc); });

    for (auto& pair : this->fc_file_names) {
        if (!pair.second.loo_temp_file) {
            this->expand_filename(fq, pair.first, pair.second, required);
            if (this->fc_rotated) {
                std::string path = pair.first + ".*";

                this->expand_filename(fq, path, pair.second, false);
            }
        } else if (pair.second.loo_fd.get() != -1) {
            fq.push_back(watch_logfile(pair.first, pair.second, required));
        }

        if (retval.fc_files.size() >= 100) {
            log_debug("too many new files, breaking...");
            break;
        }
    }

    fq.pop_to();

    this->fc_new_stats.clear();

    return retval;
}

void
file_collection::request_close(const std::shared_ptr<logfile>& lf)
{
    lf->close();
    this->fc_files_generation += 1;
}
