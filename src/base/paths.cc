/**
 * Copyright (c) 2021, Timothy Stack
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
 */

#include "config.h"

#include "paths.hh"
#include "fmt/format.h"

namespace lnav {
namespace paths {

ghc::filesystem::path dotlnav()
{
    auto home_env = getenv("HOME");
    auto xdg_config_home = getenv("XDG_CONFIG_HOME");

    if (home_env != nullptr) {
        auto home_path = ghc::filesystem::path(home_env);

        if (ghc::filesystem::is_directory(home_path)) {
            auto home_lnav = home_path / ".lnav";

            if (ghc::filesystem::is_directory(home_lnav)) {
                return home_lnav;
            }

            if (xdg_config_home != nullptr) {
                auto xdg_path = ghc::filesystem::path(xdg_config_home);

                if (ghc::filesystem::is_directory(xdg_path)) {
                    return xdg_path / "lnav";
                }
            }

            auto home_config = home_path / ".config";

            if (ghc::filesystem::is_directory(home_config)) {
                return home_config / "lnav";
            }

            return home_lnav;
        }
    }

    return ghc::filesystem::current_path();
}

ghc::filesystem::path workdir()
{
    auto subdir_name = fmt::format("lnav-user-{}-work", getuid());
    auto tmp_path = ghc::filesystem::temp_directory_path();

    return tmp_path / ghc::filesystem::path(subdir_name);
}

}
}
