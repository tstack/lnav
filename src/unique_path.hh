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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file unique_path.hh
 */

#ifndef LNAV_UNIQUE_PATH_HH
#define LNAV_UNIQUE_PATH_HH

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "filesystem/path.h"

class unique_path_source {
public:
    virtual void set_unique_path(const std::string &path) {
        this->ups_unique_path = path;
    }

    virtual std::string get_unique_path() const {
        return this->ups_unique_path;
    }

    virtual filesystem::path get_path() const = 0;

    filesystem::path& get_path_prefix() {
        return this->ups_prefix;
    }

    void set_path_prefix(const filesystem::path &prefix) {
        this->ups_prefix = prefix;
    }
private:
    filesystem::path ups_prefix;
    std::string ups_unique_path;
};

class unique_path_generator {
public:
    unique_path_generator() : upg_max_len(0) {

    };

    void add_source(std::shared_ptr<unique_path_source> path_source) {
        filesystem::path path = path_source->get_path();

        path_source->set_unique_path(path.filename());
        path_source->set_path_prefix(path.parent_path());
        this->upg_unique_paths[path.filename()].push_back(path_source);
    };

    void generate() {
        int loop_count = 0;

        while (!this->upg_unique_paths.empty()) {
            std::vector<std::shared_ptr<unique_path_source>> collisions;

            for (auto pair : this->upg_unique_paths) {
                if (pair.second.size() == 1) {
                    if (loop_count > 0) {
                        std::shared_ptr<unique_path_source> src = pair.second[0];

                        src->set_unique_path("[" + src->get_unique_path());
                    }

                    this->upg_max_len = std::max(
                        this->upg_max_len,
                        pair.second[0]->get_unique_path().size());
                } else {
                    bool all_common = true;

                    do {
                        std::string common;

                        for (auto &src : pair.second) {
                            auto &path = src->get_path_prefix();

                            if (common.empty()) {
                                common = path.filename();
                            } else if (common != path.filename()) {
                                all_common = false;
                            }
                        }

                        if (all_common) {
                            for (auto &src : pair.second) {
                                auto &path = src->get_path_prefix();

                                if (path.empty()) {
                                    all_common = false;
                                } else {
                                    src->set_path_prefix(path.parent_path());
                                }
                            }
                        }
                    } while (all_common);

                    collisions.insert(collisions.end(),
                                      pair.second.begin(),
                                      pair.second.end());
                }
            }

            this->upg_unique_paths.clear();

            for (auto &src : collisions) {
                const auto path = src->get_path();
                const auto unique_path = src->get_unique_path();
                auto &prefix = src->get_path_prefix();

                if (loop_count == 0) {
                    src->set_unique_path(prefix.filename() + "]/" + unique_path);
                } else {
                    src->set_unique_path(prefix.filename() + "/" + unique_path);
                }

                filesystem::path parent = prefix.parent_path();

                src->set_path_prefix(parent);

                if (!parent.empty()) {
                    this->upg_unique_paths[src->get_unique_path()].push_back(
                        src);
                } else {
                    src->set_unique_path("[" + src->get_unique_path());
                }
            }

            loop_count += 1;
        }
    }

    std::map<std::string, std::vector<std::shared_ptr<unique_path_source>>> upg_unique_paths;
    size_t upg_max_len;
};

#endif //LNAV_UNIQUE_PATH_HH
