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

#include <filesystem>
#include <iostream>

#include "base/fs_util.hh"

#include "config.h"
#include "doctest/doctest.h"

TEST_CASE("fs_util::build_path")
{
    auto* old_path = getenv("PATH");
    unsetenv("PATH");

    CHECK("" == lnav::filesystem::build_path({}));

    CHECK("/bin:/usr/bin"
          == lnav::filesystem::build_path({"", "/bin", "/usr/bin", ""}));
    setenv("PATH", "/usr/local/bin", 1);
    CHECK("/bin:/usr/bin:/usr/local/bin"
          == lnav::filesystem::build_path({"", "/bin", "/usr/bin", ""}));
    setenv("PATH", "/usr/local/bin:/opt/bin", 1);
    CHECK("/usr/local/bin:/opt/bin" == lnav::filesystem::build_path({}));
    CHECK("/bin:/usr/bin:/usr/local/bin:/opt/bin"
          == lnav::filesystem::build_path({"", "/bin", "/usr/bin", ""}));
    if (old_path != nullptr) {
        setenv("PATH", old_path, 1);
    }
}

TEST_CASE("fs_util::escape_path")
{
    auto p1 = std::filesystem::path{"/abc/def"};

    CHECK("/abc/def" == lnav::filesystem::escape_path(p1));

    auto p2 = std::filesystem::path{"$abc"};

    CHECK("\\$abc" == lnav::filesystem::escape_path(p2));
}
