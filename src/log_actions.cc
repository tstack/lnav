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
 */

#if 0
#    include "log_actions.hh"

#    include "base/fs_util.hh"
#    include "base/injector.hh"
#    include "bound_tags.hh"
#    include "config.h"
#    include "piper_proc.hh"

std::string
action_delegate::execute_action(const std::string& action_name)
{
    static auto& lnav_flags = injector::get<unsigned long&, lnav_flags_tag>();

    if (lnav_flags & LNF_SECURE_MODE) {
        return "unavailable in secure mode";
    }

    auto& ldh = this->ad_log_helper;
    auto value_index = this->ad_press_value;
    logline_value& lv = ldh.ldh_line_values.lvv_values[value_index];
    auto lf = ldh.ldh_file;
    const auto format = lf->get_format();
    pid_t child_pid;
    std::string retval;

    auto iter = format->lf_action_defs.find(action_name);

    const log_format::action_def& action = iter->second;

    auto_pipe in_pipe(STDIN_FILENO);
    auto_pipe out_pipe(STDOUT_FILENO);

    in_pipe.open();
    if (action.ad_capture_output) {
        out_pipe.open();
    }

    child_pid = fork();

    in_pipe.after_fork(child_pid);
    out_pipe.after_fork(child_pid);

    switch (child_pid) {
        case -1:
            retval = fmt::format(
                FMT_STRING("error: unable to fork child process -- {}"),
                strerror(errno));
            break;
        case 0: {
            const char* args[action.ad_cmdline.size() + 1];
            std::set<std::string> path_set(format->get_source_path());
            char env_buffer[64];
            int value_line;
            std::string path;

            dup2(STDOUT_FILENO, STDERR_FILENO);
            setenv("LNAV_ACTION_FILE", lf->get_filename().c_str(), 1);
            snprintf(env_buffer,
                     sizeof(env_buffer),
                     "%ld",
                     (ldh.ldh_line - lf->begin()) + 1);
            setenv("LNAV_ACTION_FILE_LINE", env_buffer, 1);
            snprintf(
                env_buffer, sizeof(env_buffer), "%d", ldh.ldh_y_offset + 1);
            setenv("LNAV_ACTION_MSG_LINE", env_buffer, 1);
            setenv("LNAV_ACTION_VALUE_NAME", lv.lv_meta.lvm_name.get(), 1);
            value_line = ldh.ldh_y_offset - ldh.get_value_line(lv) + 1;
            snprintf(env_buffer, sizeof(env_buffer), "%d", value_line);
            setenv("LNAV_ACTION_VALUE_LINE", env_buffer, 1);

            for (const auto& path_iter : path_set) {
                if (!path.empty()) {
                    path += ":";
                }
                path += path_iter;
            }
            path += ":" + std::string(getenv("PATH"));
            setenv("PATH", path.c_str(), 1);
            for (size_t lpc = 0; lpc < action.ad_cmdline.size(); lpc++) {
                args[lpc] = action.ad_cmdline[lpc].c_str();
            }
            args[action.ad_cmdline.size()] = nullptr;
            execvp(args[0], (char* const*) args);
            fprintf(stderr,
                    "error: could not exec process -- %s:%s\n",
                    args[0],
                    strerror(errno));
            _exit(0);
        } break;
        default: {
            static int exec_count = 0;

            const auto value = lv.to_string();

            this->ad_child_cb(child_pid);

            if (write(in_pipe.write_end(), value.c_str(), value.size()) == -1) {
                perror("execute_action write");
            }
            in_pipe.close();

            if (out_pipe.read_end() != -1) {
                auto pp = std::make_shared<piper_proc>(
                    std::move(out_pipe.read_end()),
                    false,
                    lnav::filesystem::open_temp_file(
                        std::filesystem::temp_directory_path()
                        / "lnav.action.XXXXXX")
                        .map([](auto pair) {
                            std::filesystem::remove(pair.first);

                            return pair;
                        })
                        .expect("Cannot create temporary file for action")
                        .second);
                auto desc = fmt::format(FMT_STRING("[{}] Output of {}"),
                                        exec_count++,
                                        action.ad_cmdline[0]);

                this->ad_piper_cb(desc, pp);
            }

            return "";
        } break;
    }

    return retval;
}

bool
action_delegate::text_handle_mouse(textview_curses& tc, mouse_event& me)
{
    bool retval = false;

    if (me.me_button != mouse_button_t::BUTTON_LEFT) {
        return false;
    }

    vis_line_t mouse_line = vis_line_t(tc.get_top() + me.me_y);
    int mouse_left = tc.get_left() + me.me_x;

    switch (me.me_state) {
        case mouse_button_state_t::BUTTON_STATE_PRESSED:
            if (mouse_line >= 0_vl && mouse_line <= tc.get_bottom()) {
                size_t line_end_index = 0;
                int x_offset;

                this->ad_press_line = mouse_line;
                this->ad_log_helper.parse_line(mouse_line, true);

                this->ad_log_helper.get_line_bounds(this->ad_line_index,
                                                    line_end_index);

                struct line_range lr(this->ad_line_index, line_end_index);

                this->ad_press_value = -1;

                x_offset = this->ad_line_index + mouse_left;
                if (lr.contains(x_offset)) {
                    for (size_t lpc = 0;
                         lpc < this->ad_log_helper.ldh_line_values.lvv_values
                                   .size();
                         lpc++)
                    {
                        auto& lv = this->ad_log_helper.ldh_line_values
                                       .lvv_values[lpc];

                        if (lv.lv_origin.contains(x_offset)) {
                            this->ad_press_value = lpc;
                            break;
                        }
                    }
                }
            }
            break;
        case mouse_button_state_t::BUTTON_STATE_DRAGGED:
            if (mouse_line != this->ad_press_line) {
                this->ad_press_value = -1;
            }
            if (this->ad_press_value != -1) {
                retval = true;
            }
            break;
        case mouse_button_state_t::BUTTON_STATE_RELEASED:
            if (this->ad_press_value != -1 && this->ad_press_line == mouse_line)
            {
                auto& lv = this->ad_log_helper.ldh_line_values
                               .lvv_values[this->ad_press_value];
                int x_offset = this->ad_line_index + mouse_left;

                if (lv.lv_origin.contains(x_offset)) {
                    auto lf = this->ad_log_helper.ldh_file;
                    const std::vector<std::string>* actions;

                    actions = lf->get_format()->get_actions(lv);
                    if (actions != nullptr && !actions->empty()) {
                        const auto rc = execute_action(actions->at(0));

                        // lnav_data.ld_rl_view->set_value(rc);
                    }
                }
                retval = true;
            }
            break;
    }

    return retval;
}
#endif
