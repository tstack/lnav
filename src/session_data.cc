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

#include "config.h"

#include <stdio.h>
#include <glob.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>

#include <openssl/sha.h>

#include <algorithm>

#include "yajlpp.hh"
#include "lnav.hh"
#include "logfile.hh"
#include "lnav_util.hh"
#include "session_data.hh"

using namespace std;

static const size_t MAX_SESSIONS = 16;

static string bookmark_file_name(const string &name)
{
    char mark_base_name[256];
    SHA_CTX context;
    byte_array<20> hash;

    SHA_Init(&context);
    SHA_Update(&context, name.c_str(), name.length());
    SHA_Final(hash.out(), &context);

    snprintf(mark_base_name, sizeof(mark_base_name),
             "file-%s.ts%ld.json",
             hash.to_string().c_str(),
             lnav_data.ld_session_time);

    return string(mark_base_name);
}

static string latest_bookmark_file(const string &name)
{
    std::vector<std::pair<int, string> > file_names;
    static_root_mem<glob_t, globfree> file_list;
    string mark_file_pattern;
    char mark_base_name[256];
    SHA_CTX context;
    byte_array<20> hash;

    SHA_Init(&context);
    SHA_Update(&context, name.c_str(), name.length());
    SHA_Final(hash.out(), &context);

    snprintf(mark_base_name, sizeof(mark_base_name),
             "file-%s.ts*.json",
             hash.to_string().c_str());

    mark_file_pattern = dotlnav_path(mark_base_name);

    if (glob(mark_file_pattern.c_str(), 0, NULL, file_list.inout()) == 0) {
        for (size_t lpc = 0; lpc < file_list->gl_pathc; lpc++) {
            const char *path = file_list->gl_pathv[lpc];
            const char *base;
            int timestamp;

            base = strrchr(path, '/') + 1;
            if (sscanf(base, "file-%*[^.].ts%d.json", &timestamp) == 1) {
                file_names.push_back(make_pair(timestamp, path));
            }
        }
    }

    sort(file_names.begin(), file_names.end());

    if (file_names.empty())
        return "";

    return file_names.back().second;
}

void init_session(void)
{
    byte_array<20> hash;
    SHA_CTX context;

    lnav_data.ld_session_time = time(NULL);

    SHA_Init(&context);
    sha_updater updater(&context);
    for_each(lnav_data.ld_file_names.begin(),
             lnav_data.ld_file_names.end(),
             object_field(updater, &pair<string, int>::first));
    SHA_Final(hash.out(), &context);

    lnav_data.ld_session_id = hash.to_string();
}

void scan_sessions(void)
{
    std::list<session_pair_t> &session_file_names = lnav_data.ld_session_file_names;
    static_root_mem<glob_t, globfree> view_info_list;
    std::list<session_pair_t>::iterator iter;
    char view_info_pattern_base[128];
    string view_info_pattern;
    string old_session_name;
    int index;

    if (lnav_data.ld_session_file_index >= 0 &&
        lnav_data.ld_session_file_index < (int)session_file_names.size()) {
        iter = session_file_names.begin();

        advance(iter, lnav_data.ld_session_file_index);
        old_session_name = iter->second;
    }

    session_file_names.clear();

    snprintf(view_info_pattern_base, sizeof(view_info_pattern_base),
             "view-info-%s.*.json",
             lnav_data.ld_session_id.c_str());
    view_info_pattern = dotlnav_path(view_info_pattern_base);
    if (glob(view_info_pattern.c_str(), 0, NULL, view_info_list.inout()) == 0) {
        for (size_t lpc = 0; lpc < view_info_list->gl_pathc; lpc++) {
            const char *path = view_info_list->gl_pathv[lpc];
            int timestamp, ppid;
            const char *base;

            base = strrchr(path, '/') + 1;
            if (sscanf(base,
                       "view-info-%*[^.].ts%d.ppid%d.json",
                       &timestamp,
                       &ppid) == 2) {
                ppid_time_pair_t ptp;

                ptp.first = (ppid == getppid()) ? 1 : 0;
                ptp.second = timestamp;
                session_file_names.push_back(make_pair(ptp, path));
            }
        }
    }

    session_file_names.sort();

    while (session_file_names.size() > MAX_SESSIONS) {
        unlink(session_file_names.front().second.c_str());
        session_file_names.pop_front();
    }

    lnav_data.ld_session_file_index = ((int)session_file_names.size()) - 1;

    for (index = 0, iter = session_file_names.begin();
         iter != session_file_names.end();
         index++, ++iter) {
        if (iter->second == old_session_name) {
            lnav_data.ld_session_file_index = index;
            break;
        }
    }
}

static int read_path(void *ctx, const unsigned char *str, size_t len)
{
    return 1;
}

static int read_marks(void *ctx, long long num)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    pair<logfile *, content_line_t> *pair;

    fprintf(stderr, "read line %qd\n", num);

    pair = (std::pair<logfile *, content_line_t> *)ypc->ypc_userdata;
    lnav_data.ld_log_source.set_user_mark(&textview_curses::BM_USER,
                                          content_line_t(pair->second + num));

    return 1;
}

static struct json_path_handler file_handlers[] = {
    json_path_handler("/path", read_path),
    json_path_handler("/marks#", read_marks),

    json_path_handler()
};

void load_bookmarks(void)
{
    logfile_sub_source::iterator iter;

    for (iter = lnav_data.ld_log_source.begin();
         iter != lnav_data.ld_log_source.end();
         ++iter) {
        pair<logfile *, content_line_t> logfile_pair;
        yajlpp_parse_context ypc(file_handlers);
        const string &log_name = iter->ld_file->get_filename();
        string mark_file_name;
        yajl_handle handle;
        int fd;

        fprintf(stderr, "load %s\n", log_name.c_str());

        logfile_pair.first = lnav_data.ld_log_source.find(log_name.c_str(),
                                                          logfile_pair.second);
        if (logfile_pair.first == NULL) {
            fprintf(stderr, "  not found\n");
            continue;
        }

        mark_file_name = latest_bookmark_file(log_name.c_str());
        if (mark_file_name.empty())
            continue;

        fprintf(stderr, "loading %s\n", mark_file_name.c_str());
        handle = yajl_alloc(&ypc.ypc_callbacks, NULL, &ypc);

        ypc.ypc_userdata = (void *)&logfile_pair;
        if ((fd = open(mark_file_name.c_str(), O_RDONLY)) < 0) {
            perror("cannot open bookmark file");
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

static int read_commands(void *ctx, const unsigned char *str, size_t len)
{
    std::string cmdline = std::string((const char *)str, len);

    execute_command(cmdline);

    return 1;
}

static struct json_path_handler view_info_handlers[] = {
    json_path_handler("/files#", read_files),
    json_path_handler("/views/([^.]+)/top_line", read_top_line),
    json_path_handler("/commands#", read_commands),

    json_path_handler()
};

void load_session(void)
{
    std::list<session_pair_t>::iterator sess_iter;
    yajlpp_parse_context ypc(view_info_handlers);
    yajl_handle handle;
    int fd;

    load_bookmarks();

    if (lnav_data.ld_session_file_names.empty()) {
        return;
    }

    handle = yajl_alloc(&ypc.ypc_callbacks, NULL, &ypc);
    sess_iter = lnav_data.ld_session_file_names.begin();
    advance(sess_iter, lnav_data.ld_session_file_index);
    string &view_info_name = sess_iter->second;

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

void save_bookmarks(void)
{
    logfile_sub_source &lss = lnav_data.ld_log_source;
    bookmarks<content_line_t>::type &bm = lss.get_user_bookmarks();
    bookmark_vector<content_line_t> &user_marks = bm[&textview_curses::BM_USER];
    logfile_sub_source::iterator file_iter;
    bookmark_vector<content_line_t>::iterator iter;
    string mark_file_name, mark_file_tmp_name;
    auto_mem<FILE> file(fclose);
    logfile *curr_lf = NULL;
    yajl_gen handle = NULL;

    for (file_iter = lnav_data.ld_log_source.begin();
         file_iter != lnav_data.ld_log_source.end();
         ++file_iter) {
        logfile *lf = file_iter->ld_file;
        string mark_base_name = bookmark_file_name(lf->get_filename());

        mark_file_name = dotlnav_path(mark_base_name.c_str());
        mark_file_tmp_name = mark_file_name + ".tmp";

        file = fopen(mark_file_tmp_name.c_str(), "w");
        handle = yajl_gen_alloc(NULL);
        yajl_gen_config(handle, yajl_gen_beautify, 1);
        yajl_gen_config(handle,
                        yajl_gen_print_callback, yajl_writer, file.in());

        {
            yajlpp_map root_map(handle);

            root_map.gen("path");
            root_map.gen(lf->get_filename());
            {
                yajlpp_array mark_array(handle);
            }
        }

        yajl_gen_clear(handle);
        yajl_gen_free(handle);
        fclose(file.release());

        rename(mark_file_tmp_name.c_str(), mark_file_name.c_str());

        handle = NULL;
    }

    for (iter = user_marks.begin(); iter != user_marks.end(); ++iter) {
        content_line_t cl = *iter;
        logfile *lf;

        lf = lss.find(cl);
        if (curr_lf != lf) {

            if (handle) {
                yajl_gen_array_close(handle);
                yajl_gen_map_close(handle);
                yajl_gen_clear(handle);
                yajl_gen_free(handle);
                fclose(file.release());

                rename(mark_file_tmp_name.c_str(), mark_file_name.c_str());
            }

            string mark_base_name = bookmark_file_name(lf->get_filename());
            mark_file_name = dotlnav_path(mark_base_name.c_str());
            mark_file_tmp_name = mark_file_name + ".tmp";

            file = fopen(mark_file_name.c_str(), "w");
            handle = yajl_gen_alloc(NULL);
            yajl_gen_config(handle, yajl_gen_beautify, 1);
            yajl_gen_config(handle,
                            yajl_gen_print_callback, yajl_writer, file.in());
            yajl_gen_map_open(handle);
            yajl_gen_string(handle, "path");
            yajl_gen_string(handle, lf->get_filename());
            yajl_gen_string(handle, "marks");
            yajl_gen_array_open(handle);

            curr_lf = lf;
        }

        yajl_gen_integer(handle, (long long)cl);
    }
    if (handle) {
        yajl_gen_array_close(handle);
        yajl_gen_map_close(handle);
        yajl_gen_clear(handle);
        yajl_gen_free(handle);
        fclose(file.release());
    }
}

void save_session(void)
{
    string view_file_name, view_file_tmp_name;
    auto_mem<FILE> file(fclose);
    char view_base_name[256];
    yajl_gen handle = NULL;

    save_bookmarks();

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

            root_map.gen("commands");

            {
                logfile_sub_source::filter_stack_t::iterator filter_iter;
                logfile_sub_source &lss = lnav_data.ld_log_source;
                logfile_sub_source::filter_stack_t &fs = lss.get_filters();

                yajlpp_array cmd_array(handle);

                for (filter_iter = fs.begin();
                     filter_iter != fs.end();
                     ++filter_iter) {
                    if (!(*filter_iter)->is_enabled())
                        continue;

                    cmd_array.gen((*filter_iter)->to_command());
                }

                textview_curses::highlight_map_t &hmap = lnav_data.ld_views[LNV_LOG].get_highlights();
                textview_curses::highlight_map_t::iterator hl_iter;

                for (hl_iter = hmap.begin();
                     hl_iter != hmap.end();
                     ++hl_iter) {
                    if (hl_iter->first[0] == '$')
                        continue;
                    cmd_array.gen("highlight " + hl_iter->first);
                }
            }
        }

        yajl_gen_clear(handle);
        yajl_gen_free(handle);

        fclose(file.release());

        rename(view_file_tmp_name.c_str(), view_file_name.c_str());
    }
}

void reset_session(void)
{
    textview_curses::highlight_map_t &hmap = lnav_data.ld_views[LNV_LOG].get_highlights();
    textview_curses::highlight_map_t::iterator hl_iter = hmap.begin();

    save_session();
    scan_sessions();

    lnav_data.ld_session_time = time(NULL);

    while (hl_iter != hmap.end()) {
        if (hl_iter->first[0] == '$') {
            ++hl_iter;
        }
        else {
            hmap.erase(hl_iter++);
        }
    }

    logfile_sub_source &lss = lnav_data.ld_log_source;

    lss.get_filters().clear();
}
