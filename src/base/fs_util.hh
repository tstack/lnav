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

#ifndef lnav_fs_util_hh
#define lnav_fs_util_hh

#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "auto_fd.hh"
#include "enum_util.hh"
#include "fmt/format.h"
#include "intern_string.hh"
#include "mapbox/variant.hpp"
#include "result.h"
#include "time_util.hh"

struct default_for_text_format {
    bool operator==(const default_for_text_format&) const { return true; }
};
struct file_location_tail {
    bool operator==(const file_location_tail&) const { return true; }
};

using file_location_t = mapbox::util::
    variant<default_for_text_format, file_location_tail, int, std::string>;

namespace lnav::filesystem {

inline bool
is_glob(const std::string& fn)
{
    return (fn.find('*') != std::string::npos
            || fn.find('?') != std::string::npos
            || fn.find('[') != std::string::npos);
}

/**
 * Expand a leading "~" or "~user" in a path to the home directory, like the
 * shell does.  The path is returned unchanged if it does not start with a
 * tilde or the user is not known.
 */
std::string expand_tilde(const std::string& path);

/**
 * @return True if a component of the given pattern is exactly "**", which
 * matches zero or more directories.
 */
bool is_recursive_glob(const std::string& fn);

/**
 * Match a path against a glob pattern where a "**" component matches zero
 * or more path components.  Other components are matched individually, so
 * a "*" does not cross a "/".
 */
bool glob_match(const std::string& pattern, const std::string& path);

/**
 * Match a file path against a user-supplied pattern.  A pattern with a "**"
 * component is matched with glob_match(), otherwise fnmatch() is used
 * without FNM_PATHNAME so that a "*" can cross a "/" and "*.log" matches a
 * file in any directory.
 */
bool matches_file_pattern(const std::string& pattern, const std::string& path);

/**
 * Split a pattern into the leading directory components that contain no
 * glob characters and the remainder.  For example, "/var/log/ ** / *.log"
 * becomes {"/var/log", "** / *.log"} (without the spaces).  The first
 * element is empty for a relative pattern that starts with a glob.
 */
std::pair<std::string, std::string> split_glob_prefix(
    const std::string& pattern);

/**
 * Expands a pattern containing a "**" component by walking the directory
 * tree.  A walk is done by iterating from begin() to end(), which reads
 * directories lazily, so a caller can stop early or give the walk a budget
 * of directories and resume it later.  The directory listings are cached
 * so that a later walk only re-reads directories whose modification time
 * changed.
 *
 * Each directory yields its matching files in sorted order before the walk
 * descends into its subdirectories, also in sorted order.
 *
 * Directories that are symbolic links are not followed and names that
 * start with a "." are skipped, unless a component of the pattern after
 * the "**" starts with a ".".
 *
 * The iterators point back at the recursive_glob, so it cannot be copied
 * or moved.
 */
class recursive_glob {
public:
    explicit recursive_glob(std::string pattern);

    recursive_glob(const recursive_glob&) = delete;
    recursive_glob& operator=(const recursive_glob&) = delete;

    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = std::string;
        using difference_type = std::ptrdiff_t;
        using pointer = const std::string*;
        using reference = const std::string&;

        iterator() = default;

        const std::string& operator*() const
        {
            return this->i_files[this->i_file_index];
        }

        const std::string* operator->() const { return &**this; }

        /** Walk to the next match, within the directory budget. */
        iterator& operator++();

        /**
         * Iterators are equal when neither is at a match, so an iterator
         * that paused on its budget compares equal to end().
         */
        bool operator==(const iterator& other) const
        {
            return !this->at_match() && !other.at_match();
        }

        bool operator!=(const iterator& other) const
        {
            return !(*this == other);
        }

        /**
         * Allow the walk to visit up to max_dirs more directories and
         * continue it if it was paused.  The max_dirs must be non-zero.
         */
        void resume(size_t max_dirs);

        /**
         * @return True if the walk visited every directory, as opposed to
         * pausing because it ran out of budget.
         */
        bool finished() const { return this->i_state == state_t::finished; }

        /**
         * @return The directory one level below the root of the walk that
         * holds the next directory to visit, or nullopt if there are none
         * left.  For example, with the pattern "/ ** / *.log" (without the
         * spaces) and the next directory "/usr/share/doc", this is "/usr".
         */
        std::optional<std::string> next_top_dir() const;

        /** @return The number of matches produced so far in this walk. */
        size_t match_count() const { return this->i_match_count; }

        /** @return The number of directories visited so far in this walk. */
        size_t dirs_visited() const { return this->i_dirs_visited; }

    private:
        friend class recursive_glob;

        enum class state_t : uint8_t {
            match,
            paused,
            finished,
        };

        bool at_match() const { return this->i_state == state_t::match; }

        void advance();

        recursive_glob* i_parent{nullptr};
        size_t i_pass{0};
        /** The directories left to visit, the next one at the back. */
        std::vector<std::string> i_dirs;
        /** The matches from the directory that was last visited. */
        std::vector<std::string> i_files;
        size_t i_file_index{0};
        size_t i_dir_budget{0};
        size_t i_match_count{0};
        size_t i_dirs_visited{0};
        state_t i_state{state_t::finished};
    };

    const std::string& get_pattern() const { return this->rg_pattern; }

    /**
     * Start a walk that visits at most max_dirs directories before
     * pausing.  The max_dirs must be non-zero.
     */
    iterator begin(size_t max_dirs = std::numeric_limits<size_t>::max());

    iterator end() const { return {}; }

private:
    struct dir_state {
        std::filesystem::file_time_type ds_mtime;
        /** The last walk that visited this directory. */
        size_t ds_pass{0};
        std::vector<std::string> ds_subdirs;
        std::vector<std::string> ds_files;
    };

    std::vector<std::string> find_roots() const;

    /**
     * @return The listing for the given directory, from the cache if the
     * directory has not been modified, or nullptr if it cannot be read.
     */
    const dir_state* lookup(const std::string& dir, size_t pass);

    /**
     * Drop the cached directories that the given walk did not visit, if it
     * is the latest walk.
     */
    void end_pass(size_t pass);

    std::string rg_pattern;
    std::string rg_prefix;
    bool rg_allow_hidden{false};
    size_t rg_pass{0};
    std::map<std::string, dir_state> rg_dirs;
};

std::string escape_glob_for_win(std::string arg);

bool is_url(const std::string& fn);

enum class path_type {
    normal,
    pattern,
    windows,
    remote,
    url,
};

std::string escape_path(const std::filesystem::path& p,
                        path_type pt = path_type::normal);

bool contains_dotdot(const std::filesystem::path& p);

path_type determine_path_type(const std::string& arg);

struct path_transcoder {
    static path_transcoder from(std::string arg);

    std::filesystem::path pt_path;
    std::optional<bool> pt_root_name_capitalized;

    std::string to_native(std::string arg);
    static std::string to_shell_arg(std::string arg);
};

std::pair<std::string, file_location_t> split_file_location(
    const std::string& path);

inline int
statp(const std::filesystem::path& path, struct stat* buf)
{
    return stat(path.c_str(), buf);
}

inline int
openp(const std::filesystem::path& path, int flags)
{
    return open(path.c_str(), flags);
}

inline int
openp(const std::filesystem::path& path, int flags, mode_t mode)
{
    return open(path.c_str(), flags, mode);
}

std::optional<std::filesystem::path> self_path();

lnav::time64_t self_mtime();

Result<std::filesystem::path, std::string> realpath(
    const std::filesystem::path& path);

Result<auto_fd, std::string> create_file(const std::filesystem::path& path,
                                         int flags,
                                         mode_t mode);

Result<auto_fd, std::string> open_file(const std::filesystem::path& path,
                                       int flags);

Result<struct stat, std::string> stat_file(const std::filesystem::path& path);

Result<std::pair<std::filesystem::path, auto_fd>, std::string> open_temp_file(
    const std::filesystem::path& pattern);

Result<std::string, std::string> read_file(const std::filesystem::path& path);

enum class write_file_options : uint8_t {
    backup_existing,
    read_only,
    executable,
};

struct write_file_result {
    std::optional<std::filesystem::path> wfr_backup_path;
};

Result<write_file_result, std::string> write_file(
    const std::filesystem::path& path,
    string_fragment_producer& content,
    lnav::enums::bitset<write_file_options> options = {});

inline Result<write_file_result, std::string>
write_file(const std::filesystem::path& path,
           const string_fragment& content,
           lnav::enums::bitset<write_file_options> options = {})
{
    auto sfp = string_fragment_producer::from(content);
    return write_file(path, *sfp, options);
}

std::string build_path(const std::vector<std::filesystem::path>& paths);

class file_lock {
public:
    class guard {
    public:
        explicit guard(file_lock* arc_lock) : g_lock(arc_lock)
        {
            this->g_lock->lock();
        }

        guard(guard&& other) noexcept
            : g_lock(std::exchange(other.g_lock, nullptr))
        {
        }

        ~guard()
        {
            if (this->g_lock != nullptr) {
                this->g_lock->unlock();
            }
        }

        guard(const guard&) = delete;
        guard& operator=(const guard&) = delete;
        guard& operator=(guard&&) = delete;

    private:
        file_lock* g_lock;
    };

    void lock() const { lockf(this->lh_fd, F_LOCK, 0); }

    void unlock() const { lockf(this->lh_fd, F_ULOCK, 0); }

    explicit file_lock(const std::filesystem::path& archive_path);

    auto_fd lh_fd;
};

}  // namespace lnav::filesystem

template<>
struct fmt::formatter<std::filesystem::path> : formatter<string_view> {
    auto format(const std::filesystem::path& p, format_context& ctx)
        -> decltype(ctx.out()) const;
};

#endif
