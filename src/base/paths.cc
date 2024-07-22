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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#ifdef __CYGWIN__
#    include <algorithm>
#    include <iostream>
#    include <sstream>
#endif

#include <unistd.h>

#include "fmt/format.h"
#include "paths.hh"

namespace lnav {
namespace paths {

#ifdef __CYGWIN__
char*
windows_to_unix_file_path(char* input)
{
    if (input == nullptr) {
        return nullptr;
    }
    std::string file_path;
    file_path.assign(input);

    // Replace the slashes
    std::replace(file_path.begin(),
                 file_path.end(),
                 WINDOWS_FILE_PATH_SEPARATOR,
                 UNIX_FILE_PATH_SEPARATOR);

    // Convert the drive letter to lowercase
    std::transform(
        file_path.begin(),
        file_path.begin() + 1,
        file_path.begin(),
        [](unsigned char character) { return std::tolower(character); });

    // Remove the colon
    const auto drive_letter = file_path.substr(0, 1);
    const auto remaining_path = file_path.substr(2, file_path.size() - 2);
    file_path = drive_letter + remaining_path;

    std::stringstream stringstream;
    stringstream << "/cygdrive/";
    stringstream << file_path;

    return const_cast<char*>(stringstream.str().c_str());
}
#endif

std::filesystem::path
dotlnav()
{
#ifdef __CYGWIN__
    auto home_env = windows_to_unix_file_path(getenv("APPDATA"));
#else
    auto home_env = getenv("HOME");
#endif
    auto xdg_config_home = getenv("XDG_CONFIG_HOME");

    if (home_env != nullptr) {
        auto home_path = std::filesystem::path(home_env);

        if (std::filesystem::is_directory(home_path)) {
            auto home_lnav = home_path / ".lnav";

            if (std::filesystem::is_directory(home_lnav)) {
                return home_lnav;
            }

            if (xdg_config_home != nullptr) {
                auto xdg_path = std::filesystem::path(xdg_config_home);

                if (std::filesystem::is_directory(xdg_path)) {
                    return xdg_path / "lnav";
                }
            }

            auto home_config = home_path / ".config";

            if (std::filesystem::is_directory(home_config)) {
                return home_config / "lnav";
            }

            return home_lnav;
        }
    }

    return std::filesystem::current_path();
}

std::filesystem::path
workdir()
{
    auto subdir_name = fmt::format(FMT_STRING("lnav-user-{}-work"), getuid());
    auto tmp_path = std::filesystem::temp_directory_path();

    return tmp_path / std::filesystem::path(subdir_name);
}

}  // namespace paths
}  // namespace lnav
