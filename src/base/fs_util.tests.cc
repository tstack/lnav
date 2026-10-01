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
#include <fstream>
#include <iostream>

#include <pwd.h>
#include <stdlib.h>
#include <unistd.h>

#include "base/fs_util.hh"

#include "config.h"
#include "doctest/doctest.h"

TEST_CASE("fs_util::to_posix_path")
{
    auto pt = lnav::filesystem::path_transcoder::from("c:\\foo\\bar");
    CHECK("/c/foo/bar" == pt.pt_path);
    CHECK_FALSE(pt.pt_root_name_capitalized.value());

    pt = lnav::filesystem::path_transcoder::from("C:\\foo\\bar");
    CHECK("/c/foo/bar" == pt.pt_path);
    CHECK(pt.pt_root_name_capitalized.value());

    pt = lnav::filesystem::path_transcoder::from("c:");
    CHECK("/c/" == pt.pt_path);

    pt = lnav::filesystem::path_transcoder::from("c:\\");
    CHECK("/c/" == pt.pt_path);

    // XXX what should this be?
    pt = lnav::filesystem::path_transcoder::from("c:foo\\bar");
    CHECK("/c/foo/bar" == pt.pt_path);

    pt = lnav::filesystem::path_transcoder::from("");
    CHECK("" == pt.pt_path);
}

#if defined(__MSYS__)
TEST_CASE("fs_util::escape_glob_for_win")
{
    CHECK(R"(c:/abc/def/*.log)"
          == lnav::filesystem::escape_glob_for_win(R"(c:\abc\def\*.log)"));
    CHECK(R"(c:/abc/def/\*.log)"
          == lnav::filesystem::escape_glob_for_win(R"(c:\abc\def\^*.log)"));
}
#endif

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

TEST_CASE("fs_util::contains_dotdot")
{
    CHECK_FALSE(
        lnav::filesystem::contains_dotdot(std::filesystem::path{"/abc/def"}));
    CHECK(lnav::filesystem::contains_dotdot(
        std::filesystem::path{"/abc/../def"}));
    CHECK_FALSE(lnav::filesystem::contains_dotdot(
        std::filesystem::path{"/abc..def"}));
}

TEST_CASE("fs_util::is_recursive_glob")
{
    CHECK(lnav::filesystem::is_recursive_glob("**"));
    CHECK(lnav::filesystem::is_recursive_glob("**/*.log"));
    CHECK(lnav::filesystem::is_recursive_glob("/var/log/**/*.log"));
    CHECK(lnav::filesystem::is_recursive_glob("/var/log/**"));
    CHECK_FALSE(lnav::filesystem::is_recursive_glob("/var/log/*.log"));
    CHECK_FALSE(lnav::filesystem::is_recursive_glob("/var/log/a**b"));
    CHECK_FALSE(lnav::filesystem::is_recursive_glob("/var/log/***/x"));
}

TEST_CASE("fs_util::glob_match")
{
    using lnav::filesystem::glob_match;

    CHECK(glob_match("/d/**/*.log", "/d/x.log"));
    CHECK(glob_match("/d/**/*.log", "/d/a/x.log"));
    CHECK(glob_match("/d/**/*.log", "/d/a/b/c/x.log"));
    CHECK_FALSE(glob_match("/d/**/*.log", "/e/a/x.log"));
    CHECK_FALSE(glob_match("/d/**/*.log", "/d/a/x.txt"));
    CHECK(glob_match("/d/**", "/d/a/b"));
    CHECK(glob_match("**/*.log", "a/b/x.log"));
    CHECK(glob_match("/d/**/c/*.log", "/d/a/b/c/x.log"));
    CHECK_FALSE(glob_match("/d/**/c/*.log", "/d/a/b/x.log"));
    CHECK(glob_match("/d/a**b/*.log", "/d/aXXb/x.log"));
    CHECK_FALSE(glob_match("/d/a**b/*.log", "/d/aX/Xb/x.log"));
    CHECK_FALSE(glob_match("/d/**/[", "/d/["));
}

TEST_CASE("fs_util::split_glob_prefix")
{
    using lnav::filesystem::split_glob_prefix;

    CHECK(split_glob_prefix("/var/log/**/*.log")
          == std::make_pair(std::string("/var/log/"), std::string("**/*.log")));
    CHECK(split_glob_prefix("/var/log/x.log")
          == std::make_pair(std::string("/var/log/"), std::string("x.log")));
    CHECK(split_glob_prefix("**/*.log")
          == std::make_pair(std::string(""), std::string("**/*.log")));
    CHECK(split_glob_prefix("/*.log")
          == std::make_pair(std::string("/"), std::string("*.log")));
}

static std::vector<std::string>
collect(lnav::filesystem::recursive_glob& rg)
{
    std::vector<std::string> retval;

    for (auto it = rg.begin(); it != rg.end(); ++it) {
        retval.emplace_back(*it);
    }

    return retval;
}

TEST_CASE("fs_util::recursive_glob")
{
    auto tmp = std::filesystem::temp_directory_path()
        / fmt::format("lnav-rglob-{}", getpid());
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "a" / "b" / "c");
    std::filesystem::create_directories(tmp / ".hidden");
    for (const auto& p : {
             tmp / "top.log",
             tmp / "a" / "one.log",
             tmp / "a" / "one.txt",
             tmp / "a" / "b" / "c" / "deep.log",
             tmp / ".hidden" / "secret.log",
             tmp / "a" / ".dot.log",
         })
    {
        std::ofstream(p.string()) << "hello\n";
    }
    std::filesystem::create_directory_symlink(tmp, tmp / "a" / "loop");
    std::filesystem::create_symlink(tmp / "top.log", tmp / "a" / "link.log");

    auto base = tmp.string();
    lnav::filesystem::recursive_glob rg(base + "/**/*.log");
    // The files in a directory come before the ones in its subdirectories.
    auto expected = std::vector<std::string>{
        base + "/top.log",
        base + "/a/link.log",
        base + "/a/one.log",
        base + "/a/b/c/deep.log",
    };
    CHECK(collect(rg) == expected);

    std::filesystem::create_directories(tmp / "a" / "new");
    std::ofstream((tmp / "a" / "new" / "fresh.log").string()) << "hello\n";
    std::filesystem::remove(tmp / "a" / "one.log");
    expected = std::vector<std::string>{
        base + "/top.log",
        base + "/a/link.log",
        base + "/a/b/c/deep.log",
        base + "/a/new/fresh.log",
    };
    CHECK(collect(rg) == expected);

    SUBCASE("stop and continue")
    {
        std::vector<std::string> actual;
        auto it = rg.begin();

        for (; it != rg.end() && actual.size() < 2; ++it) {
            actual.emplace_back(*it);
        }
        CHECK(actual.size() == 2);
        CHECK_FALSE(it.finished());
        for (; it != rg.end(); ++it) {
            actual.emplace_back(*it);
        }
        CHECK(it.finished());
        CHECK(it.match_count() == 4);
        CHECK(actual == expected);
    }

    SUBCASE("directory budget")
    {
        std::vector<std::string> actual;
        auto it = rg.begin(1);
        auto pauses = 0;

        while (true) {
            for (; it != rg.end(); ++it) {
                actual.emplace_back(*it);
            }
            if (it.finished()) {
                break;
            }
            pauses += 1;
            it.resume(1);
        }
        // tmp, a, a/b, a/b/c, a/new
        CHECK(pauses == 4);
        CHECK(it.dirs_visited() == 5);
        CHECK(actual == expected);
    }

    SUBCASE("next top dir")
    {
        auto it = rg.begin(1);

        for (; it != rg.end(); ++it) {
        }
        CHECK(it.next_top_dir() == base + "/a");
        it.resume(1);
        for (; it != rg.end(); ++it) {
        }
        // The next directory is a/b, which is under a.
        CHECK(it.next_top_dir() == base + "/a");
        it.resume(10);
        for (; it != rg.end(); ++it) {
        }
        CHECK(it.finished());
        CHECK_FALSE(it.next_top_dir().has_value());
    }

    SUBCASE("removed directory comes back")
    {
        std::filesystem::remove_all(tmp / "a" / "b");
        CHECK(collect(rg)
              == std::vector<std::string>{
                  base + "/top.log",
                  base + "/a/link.log",
                  base + "/a/new/fresh.log",
              });
        std::filesystem::create_directories(tmp / "a" / "b" / "c");
        std::ofstream((tmp / "a" / "b" / "c" / "deep.log").string())
            << "hello\n";
        CHECK(collect(rg) == expected);
    }

    lnav::filesystem::recursive_glob rg_c(base + "/**/c/*.log");
    CHECK(collect(rg_c) == std::vector<std::string>{base + "/a/b/c/deep.log"});

    lnav::filesystem::recursive_glob rg_hidden(base + "/**/.*.log");
    CHECK(collect(rg_hidden) == std::vector<std::string>{base + "/a/.dot.log"});

    lnav::filesystem::recursive_glob rg_prefix_glob(base + "/?/**/*.log");
    CHECK(collect(rg_prefix_glob)
          == std::vector<std::string>{
              base + "/a/link.log",
              base + "/a/b/c/deep.log",
              base + "/a/new/fresh.log",
          });
    // The root is the directory that matched the "?", so the top
    // directory is one below that.
    auto prefix_glob_it = rg_prefix_glob.begin(1);
    for (; prefix_glob_it != rg_prefix_glob.end(); ++prefix_glob_it) {
    }
    CHECK(prefix_glob_it.next_top_dir() == base + "/a/b");

    lnav::filesystem::recursive_glob rg_missing(base + "/nope/**/*.log");
    auto missing_it = rg_missing.begin();
    CHECK(missing_it == rg_missing.end());
    CHECK(missing_it.finished());
    CHECK(missing_it.match_count() == 0);
    CHECK(missing_it.dirs_visited() == 1);

    std::filesystem::remove_all(tmp);
}

TEST_CASE("fs_util::expand_tilde")
{
    using lnav::filesystem::expand_tilde;

    const auto* old_home = getenv("HOME");
    auto saved_home = old_home ? std::optional<std::string>(old_home)
                               : std::nullopt;
    setenv("HOME", "/home/test", 1);
    CHECK(expand_tilde("~") == "/home/test");
    CHECK(expand_tilde("~/a/**/*.log") == "/home/test/a/**/*.log");
    CHECK(expand_tilde("~lnav-no-such-user/a") == "~lnav-no-such-user/a");
    CHECK(expand_tilde("a/~/b") == "a/~/b");
    CHECK(expand_tilde("host:~/a") == "host:~/a");
    CHECK(expand_tilde("") == "");
    if (saved_home) {
        setenv("HOME", saved_home->c_str(), 1);
    } else {
        unsetenv("HOME");
    }

    auto* pw = getpwnam("root");
    if (pw != nullptr) {
        CHECK(expand_tilde("~root/a") == std::string(pw->pw_dir) + "/a");
    }
}
