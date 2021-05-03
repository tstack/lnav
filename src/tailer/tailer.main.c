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

#include <glob.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <stdarg.h>
#include <limits.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>

#include "sha-256.h"
#include "tailer.h"

struct node {
    struct node *n_succ;
    struct node *n_pred;
};

struct list {
    struct node *l_head;
    struct node *l_tail;
    struct node *l_tail_pred;
};

int is_glob(const char *fn)
{
    return (strchr(fn, '*') != NULL ||
            strchr(fn, '?') != NULL ||
            strchr(fn, '[') != NULL);
};

void list_init(struct list *l)
{
    l->l_head = (struct node *) &l->l_tail;
    l->l_tail = NULL;
    l->l_tail_pred = (struct node *) &l->l_head;
}

void list_move(struct list *dst, struct list *src)
{
    if (src->l_head->n_succ == NULL) {
        list_init(dst);
        return;
    }

    dst->l_head = src->l_head;
    dst->l_head->n_pred = (struct node *) &dst->l_head;
    dst->l_tail = NULL;
    dst->l_tail_pred = src->l_tail_pred;
    dst->l_tail_pred->n_succ = (struct node *) &dst->l_tail;

    list_init(src);
}

void list_remove(struct node *n)
{
    n->n_pred->n_succ = n->n_succ;
    n->n_succ->n_pred = n->n_pred;

    n->n_succ = NULL;
    n->n_pred = NULL;
}

struct node *list_remove_head(struct list *l)
{
    struct node *retval = NULL;

    if (l->l_head->n_succ != NULL) {
        retval = l->l_head;
        list_remove(l->l_head);
    }

    return retval;
}

void list_append(struct list *l, struct node *n)
{
    n->n_pred = l->l_tail_pred;
    n->n_succ = (struct node *) &l->l_tail;
    l->l_tail_pred->n_succ = n;
    l->l_tail_pred = n;
}

typedef enum {
    CS_INIT,
    CS_OFFERED,
    CS_TAILING,
    CS_SYNCED,
} client_state_t;

typedef enum {
    PS_UNKNOWN,
    PS_OK,
    PS_ERROR,
} path_state_t;

struct client_path_state {
    struct node cps_node;
    char *cps_path;
    path_state_t cps_last_path_state;
    struct stat cps_last_stat;
    int64_t cps_client_file_offset;
    int64_t cps_client_file_read_length;
    client_state_t cps_client_state;
    struct list cps_children;
};

struct client_path_state *create_client_path_state(const char *path)
{
    struct client_path_state *retval = malloc(sizeof(struct client_path_state));

    retval->cps_path = strdup(path);
    retval->cps_last_path_state = PS_UNKNOWN;
    memset(&retval->cps_last_stat, 0, sizeof(retval->cps_last_stat));
    retval->cps_client_file_offset = -1;
    retval->cps_client_file_read_length = 0;
    retval->cps_client_state = CS_INIT;
    list_init(&retval->cps_children);
    return retval;
}

void delete_client_path_state(struct client_path_state *cps);

void delete_client_path_list(struct list *l)
{
    struct client_path_state *child_cps;

    while ((child_cps = (struct client_path_state *) list_remove_head(l)) != NULL) {
        list_remove(&child_cps->cps_node);
        delete_client_path_state(child_cps);
    }
}

void delete_client_path_state(struct client_path_state *cps)
{
    free(cps->cps_path);
    delete_client_path_list(&cps->cps_children);
    free(cps);
}

void dump_client_path_states(struct list *path_list)
{
    struct client_path_state *curr = (struct client_path_state *) path_list->l_head;

    while (curr->cps_node.n_succ != NULL) {
        fprintf(stderr, "debug: path %s\n", curr->cps_path);
        dump_client_path_states(&curr->cps_children);

        curr = (struct client_path_state *) curr->cps_node.n_succ;
    }

    curr = (struct client_path_state *) path_list->l_tail_pred;
    while (curr->cps_node.n_pred != NULL) {
        fprintf(stderr, "debug: back path %s\n", curr->cps_path);
        dump_client_path_states(&curr->cps_children);

        curr = (struct client_path_state *) curr->cps_node.n_pred;
    }
}

void send_error(struct client_path_state *cps, char *msg, ...)
{
    char buffer[1024];
    va_list args;

    va_start(args, msg);
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);

    send_packet(STDOUT_FILENO,
                TPT_ERROR,
                TPPT_STRING, cps->cps_path,
                TPPT_STRING, buffer,
                TPPT_DONE);
}

void set_client_path_state_error(struct client_path_state *cps, const char *op)
{
    if (cps->cps_last_path_state != PS_ERROR) {
        // tell client of the problem
        send_error(cps, "unable to %s -- %s", op, strerror(errno));
    }
    cps->cps_last_path_state = PS_ERROR;
    cps->cps_client_file_offset = -1;
    cps->cps_client_state = CS_INIT;
    delete_client_path_list(&cps->cps_children);
}

static int readall(int sock, void *buf, size_t len)
{
    char *cbuf = (char *) buf;
    off_t offset = 0;

    while (len > 0) {
        ssize_t rc = read(sock, &cbuf[offset], len);

        if (rc == -1) {
            switch (errno) {
                case EAGAIN:
                case EINTR:
                    break;
                default:
                    return -1;
            }
        }
        else if (rc == 0) {
            errno = EIO;
            return -1;
        }
        else {
            len -= rc;
            offset += rc;
        }
    }

    return 0;
}

static tailer_packet_payload_type_t read_payload_type(int sock)
{
    tailer_packet_payload_type_t retval = TPPT_DONE;

    readall(sock, &retval, sizeof(retval));
    return retval;
}

static char *readstr(int sock)
{
    tailer_packet_payload_type_t payload_type = read_payload_type(sock);

    if (payload_type != TPPT_STRING) {
        fprintf(stderr, "error: expected string, got: %d\n", payload_type);
        return NULL;
    }

    int32_t length;

    if (readall(sock, &length, sizeof(length)) == -1) {
        fprintf(stderr, "error: unable to read string length\n");
        return NULL;
    }

    char *retval = malloc(length + 1);
    if (retval == NULL) {
        return NULL;
    }

    if (readall(sock, retval, length) == -1) {
        fprintf(stderr, "error: unable to read string of length: %d\n", length);
        free(retval);
        return NULL;
    }
    retval[length] = '\0';

    return retval;
}

struct list client_path_list;

struct client_path_state *find_client_path_state(struct list *path_list, const char *path)
{
    struct client_path_state *curr = (struct client_path_state *) path_list->l_head;

    while (curr->cps_node.n_succ != NULL) {
        if (strcmp(curr->cps_path, path) == 0) {
            return curr;
        }

        struct client_path_state *child =
            find_client_path_state(&curr->cps_children, path);

        if (child != NULL) {
            return child;
        }

        curr = (struct client_path_state *) curr->cps_node.n_succ;
    }

    return NULL;
}

int poll_paths(struct list *path_list)
{
    struct client_path_state *curr = (struct client_path_state *) path_list->l_head;
    int retval = 0;

    while (curr->cps_node.n_succ != NULL) {
        if (is_glob(curr->cps_path)) {
            glob_t gl;

            memset(&gl, 0, sizeof(gl));
            if (glob(curr->cps_path, 0, NULL, &gl) != 0) {
                set_client_path_state_error(curr, "glob");
            } else {
                struct list prev_children;

                list_move(&prev_children, &curr->cps_children);
                for (size_t lpc = 0; lpc < gl.gl_pathc; lpc++) {
                    struct client_path_state *child;

                    if ((child = find_client_path_state(
                        &prev_children, gl.gl_pathv[lpc])) == NULL) {
                        child = create_client_path_state(gl.gl_pathv[lpc]);
                    } else {
                        list_remove(&child->cps_node);
                    }
                    list_append(&curr->cps_children, &child->cps_node);
                }
                globfree(&gl);

                struct client_path_state *child;

                while ((child = (struct client_path_state *) list_remove_head(
                    &prev_children)) != NULL) {
                    send_error(child, "deleted");
                    delete_client_path_state(child);
                }

                retval += poll_paths(&curr->cps_children);
            }

            curr = (struct client_path_state *) curr->cps_node.n_succ;
            continue;
        }

        struct stat st;
        int rc = lstat(curr->cps_path, &st);

        if (rc == -1) {
            memset(&st, 0, sizeof(st));
            set_client_path_state_error(curr, "lstat");
        } else if (curr->cps_client_file_offset >= 0 &&
                   ((curr->cps_last_stat.st_dev != st.st_dev &&
                     curr->cps_last_stat.st_ino != st.st_ino) ||
                    (st.st_size < curr->cps_last_stat.st_size))) {
            send_error(curr, "replaced");
            set_client_path_state_error(curr, "replace");
        } else if (S_ISLNK(st.st_mode)) {
            switch (curr->cps_client_state) {
                case CS_INIT: {
                    char buffer[PATH_MAX];
                    ssize_t link_len;

                    link_len = readlink(curr->cps_path, buffer, sizeof(buffer));
                    if (link_len < 0) {
                        set_client_path_state_error(curr, "readlink");
                    } else {
                        buffer[link_len] = '\0';
                        send_packet(STDOUT_FILENO,
                                    TPT_LINK_BLOCK,
                                    TPPT_STRING, curr->cps_path,
                                    TPPT_STRING, buffer,
                                    TPPT_DONE);
                        curr->cps_client_state = CS_SYNCED;

                        if (buffer[0] == '/') {
                            struct client_path_state *child =
                                create_client_path_state(buffer);

                            fprintf(stderr, "info: monitoring link path %s\n",
                                    buffer);
                            list_append(&curr->cps_children, &child->cps_node);
                        }

                        retval += 1;
                    }
                    break;
                }
                case CS_SYNCED:
                    break;
                case CS_OFFERED:
                case CS_TAILING:
                    fprintf(stderr,
                            "internal-error: unexpected state for path -- %s\n",
                            curr->cps_path);
                    break;
            }

            retval += poll_paths(&curr->cps_children);

            curr->cps_last_path_state = PS_OK;
        } else if (S_ISREG(st.st_mode)) {
            switch (curr->cps_client_state) {
                case CS_INIT:
                case CS_TAILING: {
                    if (curr->cps_client_file_offset < st.st_size) {
                        int fd = open(curr->cps_path, O_RDONLY);

                        if (fd == -1) {
                            set_client_path_state_error(curr, "open");
                        } else {
                            char buffer[64 * 1024];
                            int64_t bytes_read = pread(
                                fd,
                                buffer, sizeof(buffer),
                                curr->cps_client_file_offset < 0 ?
                                0 :
                                curr->cps_client_file_offset);

                            if (bytes_read == -1) {
                                set_client_path_state_error(curr, "pread");
                            } else if (curr->cps_client_state == CS_INIT) {
                                uint8_t hash[SHA_256_HASH_SIZE];

                                calc_sha_256(hash, buffer, bytes_read);

                                curr->cps_client_file_read_length = bytes_read;
                                send_packet(STDOUT_FILENO,
                                            TPT_OFFER_BLOCK,
                                            TPPT_STRING, curr->cps_path,
                                            TPPT_INT64,
                                            curr->cps_client_file_offset,
                                            TPPT_INT64, bytes_read,
                                            TPPT_HASH, hash,
                                            TPPT_DONE);
                                curr->cps_client_state = CS_OFFERED;
                            } else {
                                if (curr->cps_client_file_offset < 0) {
                                    curr->cps_client_file_offset = 0;
                                }

                                send_packet(STDOUT_FILENO,
                                            TPT_TAIL_BLOCK,
                                            TPPT_STRING, curr->cps_path,
                                            TPPT_INT64, curr->cps_client_file_offset,
                                            TPPT_BITS, bytes_read, buffer,
                                            TPPT_DONE);
                                curr->cps_client_file_offset += bytes_read;
                            }
                            close(fd);

                            retval = 1;
                        }
                    }
                    break;
                }
                case CS_OFFERED: {
                    // Still waiting for the client ack
                    break;
                }
                case CS_SYNCED: {
                    fprintf(stderr, "internal-error: got synced for %s\n",
                            curr->cps_path);
                    break;
                }
            }

            curr->cps_last_path_state = PS_OK;
        } else if (S_ISDIR(st.st_mode)) {
            DIR *dir = opendir(curr->cps_path);

            if (dir == NULL) {
                set_client_path_state_error(curr, "opendir");
            } else {
                struct list prev_children;
                struct dirent *entry;

                list_move(&prev_children, &curr->cps_children);
                while ((entry = readdir(dir)) != NULL) {
                    if (strcmp(entry->d_name, ".") == 0 ||
                        strcmp(entry->d_name, "..") == 0) {
                        continue;
                    }

                    char full_path[PATH_MAX];

                    snprintf(full_path, sizeof(full_path),
                             "%s/%s",
                             curr->cps_path, entry->d_name);

                    struct client_path_state *child = find_client_path_state(&prev_children, full_path);

                    if (child == NULL) {
                        // new file
                        fprintf(stderr, "info: monitoring child path: %s\n", full_path);
                        child = create_client_path_state(full_path);
                    } else {
                        list_remove(&child->cps_node);
                    }
                    list_append(&curr->cps_children, &child->cps_node);
                }
                closedir(dir);

                struct client_path_state *child;

                while ((child = (struct client_path_state *) list_remove_head(
                    &prev_children)) != NULL) {
                    send_error(child, "deleted");
                    delete_client_path_state(child);
                }

                retval += poll_paths(&curr->cps_children);
            }

            curr->cps_last_path_state = PS_OK;
        }

        curr->cps_last_stat = st;

        curr = (struct client_path_state *) curr->cps_node.n_succ;
    }

    fflush(stderr);

    return retval;
}

int main(int argc, char *argv[])
{
    int done = 0, timeout = 0;

    list_init(&client_path_list);

    while (!done) {
        struct pollfd pfds[1];

        pfds[0].fd = STDIN_FILENO;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;

        int ready_count = poll(pfds, 1, timeout);

        if (ready_count) {
            tailer_packet_type_t type;

            if (readall(STDIN_FILENO, &type, sizeof(type)) == -1) {
                fprintf(stderr, "info: exiting...\n");
                done = 1;
            } else {
                switch (type) {
                    case TPT_OPEN_PATH:
                    case TPT_CLOSE_PATH: {
                        const char *path = readstr(STDIN_FILENO);

                        if (path == NULL) {
                            fprintf(stderr, "error: unable to get path to open\n");
                            done = 1;
                        } else if (read_payload_type(STDIN_FILENO) != TPPT_DONE) {
                            fprintf(stderr, "error: invalid open packet\n");
                            done = 1;
                        } else if (type == TPT_OPEN_PATH) {
                            struct client_path_state *cps;

                            cps = find_client_path_state(&client_path_list, path);
                            if (cps != NULL) {
                                fprintf(stderr, "warning: already monitoring -- %s\n", path);
                            } else {
                                cps = create_client_path_state(path);

                                fprintf(stderr, "info: monitoring path: %s\n", path);
                                list_append(&client_path_list, &cps->cps_node);
                            }
                        } else {
                            struct client_path_state *cps = find_client_path_state(&client_path_list, path);

                            if (cps == NULL) {
                                fprintf(stderr, "warning: path is not open: %s\n", path);
                            } else {
                                list_remove(&cps->cps_node);
                                delete_client_path_state(cps);
                            }
                        };
                        break;
                    }
                    case TPT_ACK_BLOCK:
                    case TPT_NEED_BLOCK: {
                        char *path = readstr(STDIN_FILENO);

                        // fprintf(stderr, "info: block packet path: %s\n", path);
                        if (path == NULL) {
                            fprintf(stderr, "error: unable to get block path\n");
                            done = 1;
                        } else if (read_payload_type(STDIN_FILENO) != TPPT_DONE) {
                            fprintf(stderr, "error: invalid block packet\n");
                            done = 1;
                        } else {
                            struct client_path_state *cps = find_client_path_state(&client_path_list, path);

                            if (cps == NULL) {
                                fprintf(stderr, "warning: unknown path in block packet: %s\n", path);
                            } else if (type == TPT_NEED_BLOCK) {
                                // fprintf(stderr, "info: client is tailing: %s\n", path);
                                cps->cps_client_state = CS_TAILING;
                            } else if (type == TPT_ACK_BLOCK) {
                                // fprintf(stderr, "info: client acked: %s\n", path);
                                cps->cps_client_file_offset +=
                                    cps->cps_client_file_read_length;
                                cps->cps_client_state = CS_INIT;
                            }
                            free(path);
                        }
                        break;
                    }
                    default: {
                        assert(0);
                    }
                }
            }
        }

        if (!done) {
            if (poll_paths(&client_path_list)) {
                timeout = 0;
            } else {
                timeout = 1000;
            }
        }
    }

    return EXIT_SUCCESS;
}
