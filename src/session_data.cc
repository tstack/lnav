/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file session_data.cc
 */

#include <stdio.h>
#include <glob.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>

#include <openssl/sha.h>

#include <algorithm>

#include "yajlpp.hh"
#include "lnav.hh"
#include "lnav_util.hh"
#include "session_data.hh"

using namespace std;

void init_session(void)
{
    byte_array<20> hash;
    char hash_hex[64];
    SHA_CTX context;

    lnav_data.ld_session_time = time(NULL);

    SHA_Init(&context);
    sha_updater updater(&context);
    for_each(lnav_data.ld_file_names.begin(),
             lnav_data.ld_file_names.end(),
             object_field(updater, &pair<string, int>::first));
    SHA_Final(hash.out(), &context);

    hash.to_string(hash_hex);

    lnav_data.ld_session_id = hash_hex;
}

void scan_sessions(void)
{
    static_root_mem<glob_t, globfree> view_info_list;
    char view_info_pattern_base[128];
    string view_info_pattern;

    snprintf(view_info_pattern_base, sizeof(view_info_pattern_base),
             "view-info-%s.*.json",
             lnav_data.ld_session_id.c_str());
    view_info_pattern = dotlnav_path(view_info_pattern_base);
    if (glob(view_info_pattern.c_str(), 0, NULL, view_info_list.inout()) == 0) {
        for (size_t lpc = 0; lpc < view_info_list->gl_pathc; lpc++) {
            int timestamp, ppid;
            char *base;

            base = strrchr(view_info_list->gl_pathv[lpc], '/') + 1;
            if (sscanf(base,
                       "view-info-%*[^.].ts%d.ppid%d.json",
                       &timestamp,
                       &ppid) == 2) {
                lnav_data.ld_session_file_names.push_back(make_pair(timestamp, view_info_list->gl_pathv[lpc]));
            }
        }
    }

    sort(lnav_data.ld_session_file_names.begin(),
         lnav_data.ld_session_file_names.end());
}

static int read_files(void *ctx, const unsigned char *str, size_t len)
{
    return 1;
}

static int read_top_line(void *ctx, long long value)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    const char **view_name;
    int view_index;

    view_name = find(lnav_view_strings,
                     lnav_view_strings + LNV__MAX,
                     ypc->get_path_fragment(-2));
    view_index = view_name - lnav_view_strings;
    if (view_index < LNV__MAX) {
        textview_curses &tc = lnav_data.ld_views[view_index];

        if (value != -1 && value < tc.get_inner_height())
            tc.set_top(vis_line_t(value));
    }

    return 1;
}

struct json_path_handler view_info_handlers[] = {
    json_path_handler(".files[]", read_files),
    json_path_handler(".views.*.top_line", read_top_line),

    json_path_handler::TERMINATOR
};

void load_session(void)
{
    yajlpp_parse_context ypc(view_info_handlers);
    yajl_handle handle;
    string view_info_name;
    int fd;

    if (lnav_data.ld_session_file_names.empty()) {
        return;
    }

    handle = yajl_alloc(&ypc.ypc_callbacks, NULL, &ypc);
    view_info_name = lnav_data.ld_session_file_names.back().second;
    if ((fd = open(view_info_name.c_str(), O_RDONLY)) < 0) {
        perror("cannot open session file");
    }
    else {
        unsigned char buffer[1024];
        size_t rc;

        while ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
            yajl_parse(handle, buffer, rc);
        }
        yajl_complete_parse(handle);
    }
    yajl_free(handle);
}

static void yajl_writer(void *context, const char *str, size_t len)
{
    FILE *file = (FILE *)context;

    fwrite(str, len, 1, file);
}

void save_session(void)
{
    string view_file_name, view_file_tmp_name;
    auto_mem<FILE> file(fclose);
    char view_base_name[256];
    yajl_gen handle = NULL;

    snprintf(view_base_name, sizeof(view_base_name),
             "view-info-%s.ts%ld.ppid%d.json",
             lnav_data.ld_session_id.c_str(),
             lnav_data.ld_session_time,
             getppid());

    view_file_name = dotlnav_path(view_base_name);
    view_file_tmp_name = view_file_name + ".tmp";

    if ((file = fopen(view_file_tmp_name.c_str(), "w")) == NULL) {
        perror("Unable to open session file");
    }
    else if ((handle = yajl_gen_alloc(NULL)) == NULL) {
        perror("Unable to create yajl_gen object");
    }
    else {
        yajl_gen_config(handle, yajl_gen_beautify, 1);
        yajl_gen_config(handle,
                        yajl_gen_print_callback, yajl_writer, file.in());

        {
            yajlpp_map root_map(handle);

            root_map.gen("files");

            {
                yajlpp_array file_list(handle);

                for_each(lnav_data.ld_file_names.begin(),
                         lnav_data.ld_file_names.end(),
                         object_field(file_list.gen, &pair<string, int>::first));
            }

            root_map.gen("views");

            {
                yajlpp_map top_view_map(handle);

                for (int lpc = 0; lpc < LNV__MAX; lpc++) {
                    textview_curses &tc = lnav_data.ld_views[lpc];
                    unsigned long width;
                    vis_line_t height;

                    top_view_map.gen(lnav_view_strings[lpc]);

                    yajlpp_map view_map(handle);

                    view_map.gen("top_line");

                    tc.get_dimensions(height, width);
                    if ((tc.get_top() + height) > tc.get_inner_height())
                        view_map.gen(-1);
                    else
                        view_map.gen((long long)tc.get_top());
                }
            }
        }

        yajl_gen_clear(handle);
        yajl_gen_free(handle);

        fclose(file.release());

        rename(view_file_tmp_name.c_str(), view_file_name.c_str());
    }
}
