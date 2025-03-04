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

#include "base/fs_util.hh"
#include "base/humanize.network.hh"
#include "base/isc.hh"
#include "base/itertools.hh"
#include "base/opt_util.hh"
#include "base/string_util.hh"
#include "config.h"
#include "file_converter_manager.hh"
#include "logfile.hh"
#include "service_tags.hh"
#include "tailer/tailer.looper.hh"

static std::mutex REALPATH_CACHE_MUTEX;
static std::unordered_map<std::string, std::string> REALPATH_CACHE;

void
child_poller::send_sigint()
{
    if (this->cp_child) {
        kill(this->cp_child->in(), SIGINT);
    }
}

child_poll_result_t
child_poller::poll(file_collection& fc)
{
    if (!this->cp_child) {
        return child_poll_result_t::FINISHED;
    }

    auto poll_res = std::move(this->cp_child.value()).poll();
    this->cp_child = std::nullopt;
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

file_collection::limits_t::limits_t()
{
    static constexpr rlim_t RESERVED_FDS = 32;

    struct rlimit rl;

    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        this->l_fds = rl.rlim_cur;
    } else {
        log_error("getrlimit() failed -- %s", strerror(errno));

        this->l_fds = 8192;
    }

    if (this->l_fds < RESERVED_FDS) {
        this->l_open_files = this->l_fds;
    } else {
        this->l_open_files = this->l_fds - RESERVED_FDS;
    }

    log_info(
        "fd limit: %zu; open file limit: %zu", this->l_fds, this->l_open_files);
}

const file_collection::limits_t&
file_collection::get_limits()
{
    static const limits_t INSTANCE;

    return INSTANCE;
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
        auto file_iter
            = std::find(this->fc_files.begin(), this->fc_files.end(), lf);
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
    {
        safe::ReadAccess<safe_name_to_errors> errs(*this->fc_name_to_errors);

        for (const auto& pair : *errs) {
            auto path = std::filesystem::path(pair.first).filename().string();

            if (path.length() > this->fc_largest_path_length) {
                this->fc_largest_path_length = path.length();
            }
        }
    }
    for (const auto& lf : this->fc_files) {
        const auto& path = lf->get_unique_path();

        if (path.native().length() > this->fc_largest_path_length) {
            this->fc_largest_path_length = path.native().length();
        }
    }
    for (const auto& pair : this->fc_other_files) {
        switch (pair.second.ofd_format) {
            case file_format_t::UNKNOWN:
            case file_format_t::ARCHIVE:
            case file_format_t::MULTIPLEXED:
            case file_format_t::SQLITE_DB: {
                auto bn = std::filesystem::path(pair.first).filename().string();
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
    bool do_regen = !other.fc_files.empty() || !other.fc_other_files.empty()
        || !other.fc_name_to_errors->readAccess()->empty();

    this->fc_recursive = this->fc_recursive || other.fc_recursive;
    this->fc_rotated = this->fc_rotated || other.fc_rotated;

    this->fc_synced_files.insert(other.fc_synced_files.begin(),
                                 other.fc_synced_files.end());

    std::map<std::string, file_error_info> new_errors;
    {
        safe::ReadAccess<safe_name_to_errors> errs(*other.fc_name_to_errors);

        new_errors.insert(errs->cbegin(), errs->cend());
    }
    {
        safe::WriteAccess<safe_name_to_errors> errs(*this->fc_name_to_errors);

        errs->insert(new_errors.begin(), new_errors.end());
    }
    if (!other.fc_file_names.empty()) {
        this->fc_files_generation += 1;
    }
    for (const auto& fn_pair : other.fc_file_names) {
        this->fc_file_names[fn_pair.first] = fn_pair.second;
    }
    if (!other.fc_files.empty()) {
        for (const auto& lf : other.fc_files) {
            this->fc_name_to_errors->writeAccess()->erase(lf->get_filename());
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

    if (do_regen) {
        this->regenerate_unique_file_names();
    }
}

/**
 * Functor used to compare files based on their device and inode number.
 */
struct same_file {
    explicit same_file(const std::filesystem::path& filename,
                       const struct stat& stat)
        : sf_filename(filename), sf_stat(stat)
    {
    }

    /**
     * Compare the given log file against the 'stat' given in the constructor.
     * @param  lf The log file to compare.
     * @return    True if the dev/inode values in the stat given in the
     *   constructor matches the stat in the logfile object.
     */
    bool operator()(const std::shared_ptr<logfile>& lf) const
    {
        if (lf->is_closed()) {
            return false;
        }

        if (lf->get_actual_path()
            && lf->get_actual_path().value() == this->sf_filename)
        {
            return true;
        }

        const auto& lf_loo = lf->get_open_options();

        if (lf_loo.loo_temp_dev != 0
            && this->sf_stat.st_dev == lf_loo.loo_temp_dev
            && this->sf_stat.st_ino == lf_loo.loo_temp_ino)
        {
            return true;
        }

        return this->sf_stat.st_dev == lf->get_stat().st_dev
            && this->sf_stat.st_ino == lf->get_stat().st_ino;
    }

    const std::filesystem::path& sf_filename;
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
std::optional<std::future<file_collection>>
file_collection::watch_logfile(const std::string& filename,
                               logfile_open_options& loo,
                               bool required)
{
    struct stat st;
    int rc;

    auto filename_key = loo.loo_filename.empty() ? filename : loo.loo_filename;
    if (this->fc_closed_files.count(filename)
        || this->fc_closed_files.count(filename_key))
    {
        log_trace("file is closed, ignore");
        return std::nullopt;
    }

    rc = stat(filename.c_str(), &st);

    if (rc == 0) {
        if (S_ISDIR(st.st_mode) && this->fc_recursive) {
            std::string wilddir = filename + "/*";

            if (this->fc_file_names.find(wilddir) == this->fc_file_names.end())
            {
                file_collection retval;

                retval.fc_file_names.emplace(
                    wilddir,
                    logfile_open_options()
                        .with_non_utf_visibility(false)
                        .with_visible_size_limit(256 * 1024));
                return lnav::futures::make_ready_future(std::move(retval));
            }
            return std::nullopt;
        }
        if (!S_ISREG(st.st_mode)) {
            if (required) {
                rc = -1;
                errno = EINVAL;
            } else {
                return std::nullopt;
            }
        }
        {
            safe::WriteAccess<safe_name_to_errors> errs(
                *this->fc_name_to_errors);

            auto err_iter = errs->find(filename_key);
            if (err_iter != errs->end()) {
                if (err_iter->second.fei_mtime != st.st_mtime) {
                    log_debug("clearing error info for file: %s",
                              filename_key.c_str());
                    errs->erase(err_iter);
                }
            }
        }
    }
    if (rc == -1) {
        if (required) {
            log_error("failed to open required file: %s -- %s",
                      filename.c_str(),
                      strerror(errno));
            file_collection retval;
            retval.fc_name_to_errors->writeAccess()->emplace(
                filename,
                file_error_info{
                    time(nullptr),
                    std::string(strerror(errno)),
                });
            return lnav::futures::make_ready_future(std::move(retval));
        }
        return std::nullopt;
    }

    if (this->fc_new_stats | lnav::itertools::find_if([&st](const auto& elem) {
            return st.st_ino == elem.st_ino && st.st_dev == elem.st_dev;
        }))
    {
        log_trace("same stat: %s", filename.c_str());
        // this file is probably a link that we have already scanned in this
        // pass.
        return std::nullopt;
    }

    this->fc_new_stats.emplace_back(st);

    const auto fn_path = std::filesystem::path(filename);
    const auto file_iter = std::find_if(
        this->fc_files.begin(), this->fc_files.end(), same_file(fn_path, st));

    if (file_iter == this->fc_files.end()) {
        if (this->fc_other_files.find(filename) != this->fc_other_files.end()) {
            return std::nullopt;
        }

        require(this->fc_progress.get() != nullptr);

        auto func = [filename,
                     st,
                     loo,
                     prog = this->fc_progress,
                     errs = this->fc_name_to_errors]() mutable {
            file_collection retval;

            {
                safe::ReadAccess<safe_name_to_errors> errs_inner(*errs);

                if (errs_inner->find(filename) != errs_inner->end()) {
                    // The file is broken, no reason to try and reopen
                    return retval;
                }
            }

            auto ff_res = detect_file_format(filename);

            loo.loo_file_format = ff_res.dffr_file_format;
            switch (ff_res.dffr_file_format) {
                case file_format_t::SQLITE_DB:
                    retval.fc_other_files[filename].ofd_format
                        = ff_res.dffr_file_format;
                    retval.fc_other_files[filename].ofd_details
                        = ff_res.dffr_details;
                    break;

                case file_format_t::MULTIPLEXED: {
                    log_info("%s: file is multiplexed, creating piper",
                             filename.c_str());

                    auto open_res
                        = lnav::filesystem::open_file(filename, O_RDONLY);
                    if (open_res.isOk()) {
                        auto looper_options = lnav::piper::options{};
                        looper_options.with_tail(loo.loo_tail);
                        auto create_res
                            = lnav::piper::create_looper(filename,
                                                         open_res.unwrap(),
                                                         auto_fd{-1},
                                                         looper_options);

                        if (create_res.isOk()) {
                            auto& ofd = retval.fc_other_files[filename];

                            ofd.ofd_format = ff_res.dffr_file_format;
                            ofd.ofd_details = ff_res.dffr_details;
                            retval.fc_file_names[filename] = loo;
                            retval.fc_file_names[filename].with_piper(
                                create_res.unwrap());
                        }
                    }
                    break;
                }

                case file_format_t::ARCHIVE: {
                    std::optional<
                        std::list<archive_manager::extract_progress>::iterator>
                        prog_iter_opt;

                    if (loo.loo_source == logfile_name_source::ARCHIVE) {
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
                            auto arc_path = std::filesystem::relative(
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
                        retval.fc_name_to_errors->writeAccess()->emplace(
                            filename,
                            file_error_info{
                                st.st_mtime,
                                res.unwrapErr(),
                            });
                    } else {
                        auto& ofd = retval.fc_other_files[filename];
                        ofd.ofd_format = ff_res.dffr_file_format;
                        ofd.ofd_details = ff_res.dffr_details;
                    }
                    {
                        prog_iter_opt | [&prog](auto prog_iter) {
                            prog->writeAccess()->sp_extractions.erase(
                                prog_iter);
                        };
                    }
                    break;
                }

                default: {
                    auto filename_to_open = filename;

                    loo.loo_match_details = ff_res.dffr_details;
                    auto eff = detect_mime_type(filename);

                    if (eff) {
                        auto cr = file_converter_manager::convert(eff.value(),
                                                                  filename);

                        if (cr.isErr()) {
                            retval.fc_name_to_errors->writeAccess()->emplace(
                                filename,
                                file_error_info{
                                    st.st_mtime,
                                    cr.unwrapErr(),
                                });
                            break;
                        }

                        auto convert_res = cr.unwrap();
                        retval.fc_child_pollers.emplace_back(child_poller{
                            filename,
                            std::move(convert_res.cr_child),
                            [filename,
                             st,
                             error_queue = convert_res.cr_error_queue](
                                auto& fc, auto& child) {
                                if (child.was_normal_exit()
                                    && child.exit_status() == EXIT_SUCCESS)
                                {
                                    log_info("converter[%d] exited normally",
                                             child.in());
                                    return;
                                }
                                log_error("converter[%d] exited with %d",
                                          child.in(),
                                          child.status());
                                fc.fc_name_to_errors->writeAccess()->emplace(
                                    filename,
                                    file_error_info{
                                        st.st_mtime,
                                        fmt::format(
                                            FMT_STRING("{}"),
                                            fmt::join(*error_queue, "\n")),
                                    });
                            },
                        });
                        loo.with_filename(filename);
                        loo.with_stat_for_temp(st);
                        loo.loo_format_name = eff->eff_format_name;
                        filename_to_open = convert_res.cr_destination;
                    }

                    log_info("loading new file: filename=%s", filename.c_str());

                    auto open_res = logfile::open(filename_to_open, loo);
                    if (open_res.isOk()) {
                        retval.fc_files.push_back(open_res.unwrap());
                    } else {
                        retval.fc_name_to_errors->writeAccess()->emplace(
                            filename,
                            file_error_info{
                                st.st_mtime,
                                open_res.unwrapErr(),
                            });
                    }
                    break;
                }
            }

            return retval;
        };

        return std::async(std::launch::async, std::move(func));
    }
    log_trace("file already open: %s", filename.c_str());

    auto lf = *file_iter;

    if (lf->is_valid_filename() && lf->get_filename() != filename) {
        /* The file is already loaded, but has been found under a different
         * name.  We just need to update the stored file name.
         */
        file_collection retval;

        log_info("renamed file: %s -> %s",
                 lf->get_filename().c_str(),
                 filename.c_str());
        retval.fc_renamed_files.emplace_back(lf, filename);
        return lnav::futures::make_ready_future(std::move(retval));
    }

    return std::nullopt;
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
        std::lock_guard lg(REALPATH_CACHE_MUTEX);

        if (REALPATH_CACHE.find(path) != REALPATH_CACHE.end()) {
            return;
        }
    }

    if (is_url(path)) {
        return;
    }

    auto filename_key = loo.loo_filename.empty() ? path : loo.loo_filename;
    auto glob_rc = glob(path.c_str(), GLOB_NOCHECK, nullptr, gl.inout());
    if (glob_rc == 0) {
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

        std::lock_guard lg(REALPATH_CACHE_MUTEX);
        for (size_t lpc = 0; lpc < gl->gl_pathc; lpc++) {
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
                    } else if (loo.loo_filename.empty()) {
                        file_collection retval;

                        if (gl->gl_pathc == 1) {
                            if (this->fc_name_to_errors->readAccess()->count(
                                    filename_key)
                                == 0)
                            {
                                log_error("failed to find path: %s (%s) -- %s",
                                          filename_key.c_str(),
                                          gl->gl_pathv[lpc],
                                          errmsg);
                                retval.fc_name_to_errors->writeAccess()
                                    ->emplace(filename_key,
                                              file_error_info{
                                                  time(nullptr),
                                                  errmsg,
                                              });
                            }
                        } else {
                            log_error("failed to find path: %s -- %s",
                                      path_str.c_str(),
                                      errmsg);
                            retval.fc_name_to_errors->writeAccess()->emplace(
                                path_str,
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
                auto future_opt = watch_logfile(iter->second, loo, required);
                if (future_opt) {
                    auto fut = std::move(future_opt.value());
                    if (fq.push_back(std::move(fut))
                        == lnav::progress_result_t::interrupt)
                    {
                        break;
                    }
                }
            }
        }
    } else if (glob_rc != GLOB_NOMATCH) {
        log_error("glob(%s) failed -- %s", path.c_str(), strerror(errno));
    }
}

file_collection
file_collection::rescan_files(bool required)
{
    file_collection retval;
    lnav::futures::future_queue<file_collection> fq(
        [this, &retval](std::future<file_collection>& fc) {
            try {
                auto v = fc.get();
                retval.merge(v);
            } catch (const std::exception& e) {
                log_error("rescan future exception: %s", e.what());
            } catch (...) {
                log_error("unknown exception thrown by rescan future");
            }

            if (retval.fc_files.size() < 100
                && this->fc_files.size() + retval.fc_files.size()
                    < get_limits().l_open_files)
            {
                return lnav::progress_result_t::ok;
            }
            return lnav::progress_result_t::interrupt;
        });

    this->fc_new_stats.clear();
    for (auto& pair : this->fc_file_names) {
        if (this->fc_files.size() + retval.fc_files.size()
            >= get_limits().l_open_files)
        {
            log_debug("too many files open, breaking...");
            break;
        }

        if (pair.second.loo_piper) {
            this->expand_filename(
                fq,
                pair.second.loo_piper->get_out_pattern().string(),
                pair.second,
                required);
            if (!pair.second.loo_piper.value().get_demux_id().empty()
                && this->fc_other_files.count(pair.first) == 0)
            {
                auto& ofd = retval.fc_other_files[pair.first];
                ofd.ofd_format = file_format_t::MULTIPLEXED;
                ofd.ofd_details
                    = pair.second.loo_piper.value().get_demux_details();
            }
        } else {
            this->expand_filename(fq, pair.first, pair.second, required);
            if (this->fc_rotated) {
                std::string path = pair.first + ".*";

                this->expand_filename(fq, path, pair.second, false);
            }
        }

        if (retval.fc_files.size() >= 100) {
            log_debug("too many new files, breaking...");
            break;
        }
    }

    fq.pop_to();

    return retval;
}

void
file_collection::request_close(const std::shared_ptr<logfile>& lf)
{
    lf->close();
    this->fc_files_generation += 1;
}

size_t
file_collection::initial_indexing_pipers() const
{
    size_t retval = 0;

    for (const auto& pair : this->fc_file_names) {
        if (pair.second.loo_piper
            && pair.second.loo_piper->get_loop_count() == 0)
        {
            retval += 1;
        }
    }

    return retval;
}

size_t
file_collection::active_pipers() const
{
    size_t retval = 0;
    for (const auto& pair : this->fc_file_names) {
        if (pair.second.loo_piper && !pair.second.loo_piper->is_finished()) {
            retval += 1;
        }
    }

    return retval;
}

size_t
file_collection::finished_pipers()
{
    size_t retval = 0;

    for (auto& pair : this->fc_file_names) {
        if (pair.second.loo_piper) {
            retval += pair.second.loo_piper->consume_finished();
        }
    }

    return retval;
}

file_collection
file_collection::copy()
{
    file_collection retval;

    retval.merge(*this);
    retval.fc_progress = this->fc_progress;
    return retval;
}

void
file_collection::clear()
{
    this->fc_name_to_errors->writeAccess()->clear();
    this->fc_file_names.clear();
    this->fc_files.clear();
    this->fc_renamed_files.clear();
    this->fc_closed_files.clear();
    this->fc_other_files.clear();
    this->fc_new_stats.clear();
}

size_t
file_collection::other_file_format_count(file_format_t ff) const
{
    size_t retval = 0;

    for (const auto& pair : this->fc_other_files) {
        if (pair.second.ofd_format == ff) {
            retval += 1;
        }
    }

    return retval;
}
