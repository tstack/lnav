/**
 * Copyright (c) 2025, Timothy Stack
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

#include <string>
#include <vector>

#include "apps.hh"

#include "apps.cfg.hh"
#include "base/injector.hh"
#include "base/lnav_log.hh"
#include "fmt/format.h"

namespace lnav::apps {

std::vector<std::string>
get_app_names()
{
    static const auto& cfg = injector::get<const config&>();

    std::vector<std::string> retval;
    for (const auto& pd : cfg.c_publishers) {
        for (const auto& ad : pd.second.pd_apps) {
            retval.emplace_back(
                fmt::format(FMT_STRING("{}/{}"), pd.first, ad.first));
        }
    }
    return retval;
}

std::filesystem::path
app_def::get_root_path() const
{
    auto cfg_path = std::filesystem::path(
                        this->ad_root_path.pp_location.sl_source.c_str())
                        .parent_path();
    return cfg_path / this->ad_root_path.pp_value;
}

std::vector<app_files>
find_app_files()
{
    static const auto& cfg = injector::get<const config&>();

    std::vector<app_files> retval;

    log_info("finding app files");
    for (const auto& pub : cfg.c_publishers) {
        for (const auto& app : pub.second.pd_apps) {
            auto af = app_files{
                fmt::format(FMT_STRING("{}/{}"), pub.first, app.first),
            };

            log_trace("  app: %s", af.af_name.c_str());
            const auto& ad = app.second;
            auto root = ad.get_root_path();
            std::error_code ec;

            for (const auto& entry :
                 std::filesystem::recursive_directory_iterator(root, ec))
            {
                auto app_path = std::filesystem::relative(entry.path(), root);
                log_trace("    file: %s - %s",
                          app_path.c_str(),
                          entry.path().c_str());
                af.af_files.emplace_back(app_path, entry.path());
            }
            if (ec) {
                log_error("Unable to read app directory: %s - %s",
                          root.c_str(),
                          ec.message().c_str());
            }
            if (af.af_files.empty()) {
                log_warning("  no files for app: %s", af.af_name.c_str());
            } else {
                retval.emplace_back(af);
            }
        }
    }

    return retval;
}

}  // namespace lnav::apps
