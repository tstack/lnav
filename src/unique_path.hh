/**
 * Copyright (c) 2018, Timothy Stack
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
 * @file unique_path.hh
 */

#ifndef LNAV_UNIQUE_PATH_HH
#define LNAV_UNIQUE_PATH_HH

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

/**
 * A source of a path for the unique_path_generator.
 */
class unique_path_source {
public:
    virtual ~unique_path_source() = default;

    void set_unique_path(const std::string& path)
    {
        this->ups_unique_path = path;
    }

    const std::filesystem::path& get_unique_path() const
    {
        return this->ups_unique_path;
    }

    virtual std::filesystem::path get_path() const = 0;

    const std::filesystem::path& get_path_prefix() const
    {
        return this->ups_prefix;
    }

    void set_path_prefix(const std::filesystem::path& prefix)
    {
        this->ups_prefix = prefix;
    }

private:
    std::filesystem::path ups_prefix;
    std::filesystem::path ups_unique_path;
};

/**
 * Given a collection of filesystem paths, this class will generate a shortened
 * and unique path for each of the given paths.
 */
class unique_path_generator {
public:
    void add_source(const std::shared_ptr<unique_path_source>& path_source);

    void generate();

    std::map<std::string, std::vector<std::shared_ptr<unique_path_source>>>
        upg_unique_paths;
    size_t upg_max_len{0};
};

#endif  // LNAV_UNIQUE_PATH_HH
