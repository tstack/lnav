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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file file_collection.cc
 */

#include "config.h"

#include <glob.h>

#include <unordered_map>

#include "base/opt_util.hh"
#include "base/humanize.network.hh"
#include "base/isc.hh"
#include "logfile.hh"
#include "file_collection.hh"
#include "pcrepp/pcrepp.hh"
#include "tailer/tailer.looper.hh"
#include "service_tags.hh"
#include "lnav_util.hh"

static std::mutex REALPATH_CACHE_MUTEX;
static std::unordered_map<std::string, std::string> REALPATH_CACHE;

void file_collection::close_files(const std::vector<std::shared_ptr<logfile>> &files)
{
    for (const auto& lf : files) {
        auto actual_path_opt = lf->get_actual_path();

        if (actual_path_opt) {
            std::lock_guard<std::mutex> lg(REALPATH_CACHE_MUTEX);
            auto path_str = actual_path_opt.value().string();

            for (auto iter = REALPATH_CACHE.begin();
                 iter != REALPATH_CACHE.end();) {
                if (iter->first == path_str || iter->second == path_str) {
                    iter = REALPATH_CACHE.erase(iter);
                } else {
                    ++iter;
                }
            }

        } else {
            this->fc_file_names.erase(lf->get_filename());
        }
        auto file_iter = find(this->fc_files.begin(),
                              this->fc_files.end(),
                              lf);
        if (file_iter != this->fc_files.end()) {
            this->fc_files.erase(file_iter);
        }
    }
    this->fc_files_generation += 1;

    this->regenerate_unique_file_names();
}

void file_collection::regenerate_unique_file_names()
{
    unique_path_generator upg;

    for (const auto &lf : this->fc_files) {
        upg.add_source(lf);
    }

    upg.generate();

    this->fc_largest_path_length = 0;
    for (const auto &pair : this->fc_name_to_errors) {
        auto path = ghc::filesystem::path(pair.first).filename().string();

        if (path.length() > this->fc_largest_path_length) {
            this->fc_largest_path_length = path.length();
        }
    }
    for (const auto &lf : this->fc_files) {
        const auto &path = lf->get_unique_path();

        if (path.length() > this->fc_largest_path_length) {
            this->fc_largest_path_length = path.length();
        }
    }
    for (const auto &pair : this->fc_other_files) {
        auto bn = ghc::filesystem::path(pair.first).filename().string();
        if (bn.length() > this->fc_largest_path_length) {
            this->fc_largest_path_length = bn.length();
        }
    }
}

void file_collection::merge(const file_collection &other)
{
    this->fc_recursive = this->fc_recursive || other.fc_recursive;
    this->fc_rotated = this->fc_rotated || other.fc_rotated;

    this->fc_synced_files.insert(other.fc_synced_files.begin(),
                                 other.fc_synced_files.end());
    this->fc_name_to_errors.insert(other.fc_name_to_errors.begin(),
                                   other.fc_name_to_errors.end());
    this->fc_file_names.insert(other.fc_file_names.begin(),
                               other.fc_file_names.end());
    if (!other.fc_files.empty()) {
        for (const auto& lf : other.fc_files) {
            this->fc_name_to_errors.erase(lf->get_filename());
        }
        this->fc_files.insert(this->fc_files.end(),
                              other.fc_files.begin(),
                              other.fc_files.end());
        this->fc_files_generation += 1;
    }
    for (auto &pair : other.fc_renamed_files) {
        pair.first->set_filename(pair.second);
    }
    this->fc_closed_files.insert(other.fc_closed_files.begin(),
                                 other.fc_closed_files.end());
    this->fc_other_files.insert(other.fc_other_files.begin(),
                                other.fc_other_files.end());
}

/**
 * Functor used to compare files based on their device and inode number.
 */
struct same_file {
    explicit same_file(const struct stat &stat) : sf_stat(stat) {};

    /**
     * Compare the given log file against the 'stat' given in the constructor.
     * @param  lf The log file to compare.
     * @return    True if the dev/inode values in the stat given in the
     *   constructor matches the stat in the logfile object.
     */
    bool operator()(const std::shared_ptr<logfile> &lf) const
    {
        return this->sf_stat.st_dev == lf->get_stat().st_dev &&
               this->sf_stat.st_ino == lf->get_stat().st_ino;
    };

    const struct stat &sf_stat;
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
file_collection::watch_logfile(const std::string &filename,
                               logfile_open_options &loo, bool required)
{
    file_collection retval;
    struct stat st;
    int rc;

    if (this->fc_closed_files.count(filename)) {
        return lnav::futures::make_ready_future(retval);
    }

    if (loo.loo_fd != -1) {
        rc = fstat(loo.loo_fd, &st);
    } else {
        rc = stat(filename.c_str(), &st);
    }

    if (rc == 0) {
        if (S_ISDIR(st.st_mode) && this->fc_recursive) {
            std::string wilddir = filename + "/*";

            if (this->fc_file_names.find(wilddir) ==
                this->fc_file_names.end()) {
                retval.fc_file_names.emplace(wilddir, logfile_open_options());
            }
            return lnav::futures::make_ready_future(retval);
        }
        if (!S_ISREG(st.st_mode)) {
            if (required) {
                rc = -1;
                errno = EINVAL;
            } else {
                return lnav::futures::make_ready_future(retval);
            }
        }
    }
    if (rc == -1) {
        if (required) {
            retval.fc_name_to_errors[filename] = strerror(errno);
        }
        return lnav::futures::make_ready_future(retval);
    }

    auto stat_iter = find_if(this->fc_new_stats.begin(),
                             this->fc_new_stats.end(),
                             [&st](auto& elem) {
                                 return st.st_ino == elem.st_ino &&
                                        st.st_dev == elem.st_dev;
                             });
    if (stat_iter != this->fc_new_stats.end()) {
        // this file is probably a link that we have already scanned in this
        // pass.
        return lnav::futures::make_ready_future(retval);
    }

    this->fc_new_stats.emplace_back(st);
    auto file_iter = find_if(this->fc_files.begin(),
                             this->fc_files.end(),
                             same_file(st));

    if (file_iter == this->fc_files.end()) {
        if (this->fc_other_files.find(filename) != this->fc_other_files.end()) {
            return lnav::futures::make_ready_future(retval);
        }

        auto func = [filename, loo, prog = this->fc_progress, errs = this->fc_name_to_errors]() mutable {
            file_collection retval;

            if (errs.find(filename) != errs.end()) {
                // The file is broken, no reason to try and reopen
                return retval;
            }

            auto ff = detect_file_format(filename);

            switch (ff) {
                case file_format_t::FF_SQLITE_DB:
                    retval.fc_other_files[filename].ofd_format = ff;
                    break;

                case file_format_t::FF_ARCHIVE: {
                    nonstd::optional<std::list<archive_manager::extract_progress>::iterator>
                        prog_iter_opt;

                    if (loo.loo_source == logfile_name_source::ARCHIVE) {
                        // Don't try to open nested archives
                        return retval;
                    }

                    auto res = archive_manager::walk_archive_files(
                        filename,
                        [prog, &prog_iter_opt](
                            const auto &path,
                            const auto total) {
                            safe::WriteAccess<safe_scan_progress> sp(*prog);

                            prog_iter_opt | [&sp](auto prog_iter) {
                                sp->sp_extractions.erase(prog_iter);
                            };
                            auto prog_iter = sp->sp_extractions.emplace(
                                sp->sp_extractions.begin(),
                                path, total);
                            prog_iter_opt = prog_iter;

                            return &(*prog_iter);
                        },
                        [&filename, &retval](
                            const auto &tmp_path,
                            const auto &entry) {
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
                        retval.fc_name_to_errors[filename] = res.unwrapErr();
                    } else {
                        retval.fc_other_files[filename] = ff;
                    }
                    {
                        prog_iter_opt |
                        [&prog](auto prog_iter) {
                            prog->writeAccess()->sp_extractions.erase(
                                prog_iter);
                        };
                    }
                    break;
                }

                default:
                    log_info("loading new file: filename=%s", filename.c_str());

                    auto open_res = logfile::open(filename, loo);
                    if (open_res.isOk()) {
                        retval.fc_files.push_back(open_res.unwrap());
                    }
                    else {
                        retval.fc_name_to_errors[filename] = open_res.unwrapErr();
                    }
                    break;
            }

            return retval;
        };

        return std::async(std::launch::async, func);
    } else {
        auto lf = *file_iter;

        if (lf->is_valid_filename() && lf->get_filename() != filename) {
            /* The file is already loaded, but has been found under a different
             * name.  We just need to update the stored file name.
             */
            retval.fc_renamed_files.emplace_back(lf, filename);
        }
    }

    return lnav::futures::make_ready_future(retval);
}

/**
 * Expand a glob pattern and call watch_logfile with the file names that match
 * the pattern.
 * @param path     The glob pattern to expand.
 * @param required Passed to watch_logfile.
 */
void file_collection::expand_filename(lnav::futures::future_queue<file_collection> &fq,
                                      const std::string &path,
                                      logfile_open_options &loo,
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

                    isc::to<tailer::looper &, services::remote_tailer_t>()
                        .send([=](auto &tlooper) {
                            tlooper.add_remote(rp, loo);
                        });
                    retval.fc_other_files[path] = file_format_t::FF_REMOTE;
                    {
                        this->fc_progress->writeAccess()->
                            sp_tailers[fmt::format("{}", rp.home())].tp_message =
                            "Initializing...";
                    }

                    fq.push_back(lnav::futures::make_ready_future(retval));
                    return;
                }

                required = false;
            }
        }
        if (gl->gl_pathc > 1 ||
            strcmp(path.c_str(), gl->gl_pathv[0]) != 0) {
            required = false;
        }

        std::lock_guard<std::mutex> lg(REALPATH_CACHE_MUTEX);
        for (lpc = 0; lpc < (int) gl->gl_pathc; lpc++) {
            auto path_str = std::string(gl->gl_pathv[lpc]);
            auto iter = REALPATH_CACHE.find(path_str);

            if (iter == REALPATH_CACHE.end()) {
                auto_mem<char> abspath;

                if ((abspath = realpath(gl->gl_pathv[lpc], nullptr)) ==
                    nullptr) {
                    if (required) {
                        fprintf(stderr, "Cannot find file: %s -- %s",
                                gl->gl_pathv[lpc], strerror(errno));
                    }
                    continue;
                } else {
                    auto p = REALPATH_CACHE.emplace(path_str, abspath.in());

                    iter = p.first;
                }
            }

            if (required || access(iter->second.c_str(), R_OK) == 0) {
                fq.push_back(watch_logfile(iter->second, loo, required));
            }
        }
    }
}

file_collection file_collection::rescan_files(bool required)
{
    file_collection retval;
    lnav::futures::future_queue<file_collection> fq([&retval](auto &fc) {
        retval.merge(fc);
    });

    for (auto &pair : this->fc_file_names) {
        if (pair.second.loo_fd == -1) {
            this->expand_filename(fq, pair.first, pair.second, required);
            if (this->fc_rotated) {
                std::string path = pair.first + ".*";

                this->expand_filename(fq, path, pair.second, false);
            }
        } else {
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
