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

#ifndef __COSMOPOLITAN__
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
#include <fcntl.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>
#include <stdint.h>
#endif

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
    int64_t cps_client_file_size;
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
    retval->cps_client_file_size = 0;
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

typedef enum {
    RS_ERROR,
    RS_PACKET_TYPE,
    RS_PAYLOAD_TYPE,
    RS_PAYLOAD,
    RS_PAYLOAD_LENGTH,
    RS_PAYLOAD_CONTENT,
} recv_state_t;

static recv_state_t readall(recv_state_t state, int sock, void *buf, size_t len)
{
    char *cbuf = (char *) buf;
    off_t offset = 0;

    if (state == RS_ERROR) {
        return RS_ERROR;
    }

    while (len > 0) {
        ssize_t rc = read(sock, &cbuf[offset], len);

        if (rc == -1) {
            if (errno == EAGAIN || errno == EINTR) {

            } else {
                return RS_ERROR;
            }
        }
        else if (rc == 0) {
            errno = EIO;
            return RS_ERROR;
        }
        else {
            len -= rc;
            offset += rc;
        }
    }

    switch (state) {
        case RS_PACKET_TYPE:
            return RS_PAYLOAD_TYPE;
        case RS_PAYLOAD_TYPE:
            return RS_PAYLOAD;
        case RS_PAYLOAD_LENGTH:
            return RS_PAYLOAD_CONTENT;
        case RS_PAYLOAD_CONTENT:
            return RS_PAYLOAD_TYPE;
        default:
            return RS_ERROR;
    }
}

static tailer_packet_payload_type_t read_payload_type(recv_state_t *state, int sock)
{
    tailer_packet_payload_type_t retval = TPPT_DONE;

    assert(*state == RS_PAYLOAD_TYPE);

    *state = readall(*state, sock, &retval, sizeof(retval));
    if (*state != RS_ERROR && retval == TPPT_DONE) {
        *state = RS_PACKET_TYPE;
    }
    return retval;
}

static char *readstr(recv_state_t *state, int sock)
{
    assert(*state == RS_PAYLOAD_TYPE);

    tailer_packet_payload_type_t payload_type = read_payload_type(state, sock);

    if (payload_type != TPPT_STRING) {
        fprintf(stderr, "error: expected string, got: %d\n", payload_type);
        return NULL;
    }

    int32_t length;

    *state = RS_PAYLOAD_LENGTH;
    *state = readall(*state, sock, &length, sizeof(length));
    if (*state == RS_ERROR) {
        fprintf(stderr, "error: unable to read string length\n");
        return NULL;
    }

    char *retval = malloc(length + 1);
    if (retval == NULL) {
        return NULL;
    }

    *state = readall(*state, sock, retval, length);
    if (*state == RS_ERROR) {
        fprintf(stderr, "error: unable to read string of length: %d\n", length);
        free(retval);
        return NULL;
    }
    retval[length] = '\0';

    return retval;
}

static int readint64(recv_state_t *state, int sock, int64_t *i)
{
    tailer_packet_payload_type_t payload_type = read_payload_type(state, sock);

    if (payload_type != TPPT_INT64) {
        fprintf(stderr, "error: expected int64, got: %d\n", payload_type);
        return -1;
    }

    *state = RS_PAYLOAD_CONTENT;
    *state = readall(*state, sock, i, sizeof(*i));
    if (*state == -1) {
        fprintf(stderr, "error: unable to read int64\n");
        return -1;
    }

    return 0;
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

void send_preview_error(int64_t id, const char *path, const char *msg)
{
    send_packet(STDOUT_FILENO,
                TPT_PREVIEW_ERROR,
                TPPT_INT64, id,
                TPPT_STRING, path,
                TPPT_STRING, msg,
                TPPT_DONE);
}

void send_preview_data(int64_t id, const char *path, int32_t len, const char *bits)
{
    send_packet(STDOUT_FILENO,
                TPT_PREVIEW_DATA,
                TPPT_INT64, id,
                TPPT_STRING, path,
                TPPT_BITS, len, bits,
                TPPT_DONE);
}

int poll_paths(struct list *path_list, struct client_path_state *root_cps)
{
    struct client_path_state *curr = (struct client_path_state *) path_list->l_head;
    int is_top = root_cps == NULL;
    int retval = 0;

    while (curr->cps_node.n_succ != NULL) {
        if (is_top) {
            root_cps = curr;
        }

        if (is_glob(curr->cps_path)) {
            int changes = 0;
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
                        changes += 1;
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
                    changes += 1;
                }

                retval += poll_paths(&curr->cps_children, root_cps);
            }

            if (changes) {
                curr->cps_client_state = CS_INIT;
            } else if (curr->cps_client_state != CS_SYNCED) {
                send_packet(STDOUT_FILENO,
                            TPT_SYNCED,
                            TPPT_STRING, root_cps->cps_path,
                            TPPT_STRING, curr->cps_path,
                            TPPT_DONE);
                curr->cps_client_state = CS_SYNCED;
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
                                    TPPT_STRING, root_cps->cps_path,
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

            retval += poll_paths(&curr->cps_children, root_cps);

            curr->cps_last_path_state = PS_OK;
        } else if (S_ISREG(st.st_mode)) {
            switch (curr->cps_client_state) {
                case CS_INIT:
                case CS_TAILING:
                case CS_SYNCED: {
                    if (curr->cps_client_file_offset < st.st_size) {
                        int fd = open(curr->cps_path, O_RDONLY);

                        if (fd == -1) {
                            set_client_path_state_error(curr, "open");
                        } else {
                            static unsigned char buffer[4 * 1024 * 1024];

                            int64_t file_offset =
                                curr->cps_client_file_offset < 0 ?
                                0 :
                                curr->cps_client_file_offset;
                            int64_t nbytes = sizeof(buffer);
                            if (curr->cps_client_state == CS_INIT) {
                                if (curr->cps_client_file_size == 0) {
                                    // initial state, haven't heard from client yet.
                                    nbytes = 32 * 1024;
                                } else if (file_offset < curr->cps_client_file_size) {
                                    // heard from client, try to catch up
                                    nbytes = curr->cps_client_file_size - file_offset;
                                    if (nbytes > sizeof(buffer)) {
                                        nbytes = sizeof(buffer);
                                    }
                                }
                            }
                            int32_t bytes_read = pread(fd, buffer, nbytes, file_offset);

                            if (bytes_read == -1) {
                                set_client_path_state_error(curr, "pread");
                            } else if (curr->cps_client_state == CS_INIT &&
                                       (curr->cps_client_file_offset < 0 ||
                                        bytes_read > 0)) {
                                static unsigned char
                                    HASH_BUFFER[4 * 1024 * 1024];
                                BYTE hash[SHA256_BLOCK_SIZE];
                                size_t remaining = 0;
                                int64_t remaining_offset
                                    = file_offset + bytes_read;
                                SHA256_CTX shactx;

                                if (curr->cps_client_file_size > 0
                                    && file_offset < curr->cps_client_file_size)
                                {
                                    remaining = curr->cps_client_file_size
                                        - file_offset - bytes_read;
                                }

                                fprintf(stderr,
                                        "info: prepping offer: init=%d; "
                                        "remaining=%zu; %s\n",
                                        bytes_read,
                                        remaining,
                                        curr->cps_path);
                                sha256_init(&shactx);
                                sha256_update(&shactx, buffer, bytes_read);
                                while (remaining > 0) {
                                    nbytes = sizeof(HASH_BUFFER);
                                    if (remaining < nbytes) {
                                        nbytes = remaining;
                                    }
                                    ssize_t remaining_bytes_read
                                        = pread(fd,
                                                HASH_BUFFER,
                                                nbytes,
                                                remaining_offset);
                                    if (remaining_bytes_read < 0) {
                                        set_client_path_state_error(curr, "pread");
                                        break;
                                    }
                                    if (remaining_bytes_read == 0) {
                                        remaining = 0;
                                        break;
                                    }
                                    sha256_update(&shactx, HASH_BUFFER, remaining_bytes_read);
                                    remaining -= remaining_bytes_read;
                                    remaining_offset += remaining_bytes_read;
                                    bytes_read += remaining_bytes_read;
                                }

                                if (remaining == 0) {
                                    sha256_final(&shactx, hash);

                                    send_packet(STDOUT_FILENO,
                                                TPT_OFFER_BLOCK,
                                                TPPT_STRING, root_cps->cps_path,
                                                TPPT_STRING, curr->cps_path,
                                                TPPT_INT64,
                                                (int64_t) st.st_mtime,
                                                TPPT_INT64, file_offset,
                                                TPPT_INT64, (int64_t) bytes_read,
                                                TPPT_HASH, hash,
                                                TPPT_DONE);
                                    curr->cps_client_state = CS_OFFERED;
                                }
                            } else {
                                if (curr->cps_client_file_offset < 0) {
                                    curr->cps_client_file_offset = 0;
                                }

                                send_packet(STDOUT_FILENO,
                                            TPT_TAIL_BLOCK,
                                            TPPT_STRING, root_cps->cps_path,
                                            TPPT_STRING, curr->cps_path,
                                            TPPT_INT64, (int64_t) st.st_mtime,
                                            TPPT_INT64, curr->cps_client_file_offset,
                                            TPPT_BITS, bytes_read, buffer,
                                            TPPT_DONE);
                                curr->cps_client_file_offset += bytes_read;
                                curr->cps_client_state = CS_TAILING;
                            }
                            close(fd);

                            retval = 1;
                        }
                    } else if (curr->cps_client_state != CS_SYNCED) {
                        send_packet(STDOUT_FILENO,
                                    TPT_SYNCED,
                                    TPPT_STRING, root_cps->cps_path,
                                    TPPT_STRING, curr->cps_path,
                                    TPPT_DONE);
                        curr->cps_client_state = CS_SYNCED;
                    }
                    break;
                }
                case CS_OFFERED: {
                    // Still waiting for the client ack
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
                int changes = 0;

                list_move(&prev_children, &curr->cps_children);
                while ((entry = readdir(dir)) != NULL) {
                    if (strcmp(entry->d_name, ".") == 0 ||
                        strcmp(entry->d_name, "..") == 0) {
                        continue;
                    }

                    if (entry->d_type != DT_REG &&
                        entry->d_type != DT_LNK) {
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
                        changes += 1;
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
                    changes += 1;
                }

                retval += poll_paths(&curr->cps_children, root_cps);

                if (changes) {
                    curr->cps_client_state = CS_INIT;
                } else if (curr->cps_client_state != CS_SYNCED) {
                    send_packet(STDOUT_FILENO,
                                TPT_SYNCED,
                                TPPT_STRING, root_cps->cps_path,
                                TPPT_STRING, curr->cps_path,
                                TPPT_DONE);
                    curr->cps_client_state = CS_SYNCED;
                }
            }

            curr->cps_last_path_state = PS_OK;
        }

        curr->cps_last_stat = st;

        curr = (struct client_path_state *) curr->cps_node.n_succ;
    }

    fflush(stderr);

    return retval;
}

static
void send_possible_paths(const char *glob_path, int depth)
{
    glob_t gl;

    memset(&gl, 0, sizeof(gl));
    if (glob(glob_path, GLOB_MARK, NULL, &gl) == 0) {
        for (size_t lpc = 0;
             lpc < gl.gl_pathc;
             lpc++) {
            const char *child_path = gl.gl_pathv[lpc];
            size_t child_len = strlen(gl.gl_pathv[lpc]);

            send_packet(STDOUT_FILENO,
                        TPT_POSSIBLE_PATH,
                        TPPT_STRING, child_path,
                        TPPT_DONE);

            if (depth == 0 && child_path[child_len - 1] == '/') {
                char *child_copy = malloc(child_len + 2);

                strcpy(child_copy, child_path);
                strcat(child_copy, "*");
                send_possible_paths(child_copy, depth + 1);
                free(child_copy);
            }
        }
    }

    globfree(&gl);
}

static
void handle_load_preview_request(const char *path, int64_t preview_id)
{
    struct stat st;

    fprintf(stderr, "info: load preview request -- %lld\n", preview_id);
    if (is_glob(path)) {
        glob_t gl;

        memset(&gl, 0, sizeof(gl));
        if (glob(path, 0, NULL, &gl) != 0) {
            char msg[1024];

            snprintf(msg, sizeof(msg),
                     "error: cannot glob %s -- %s",
                     path,
                     strerror(errno));
            send_preview_error(preview_id, path, msg);
        } else {
            char *bits = malloc(1024 * 1024);
            int lpc, line_count = 10;

            bits[0] = '\0';
            for (lpc = 0;
                 line_count > 0 && lpc < gl.gl_pathc;
                 lpc++, line_count--) {
                strcat(bits, gl.gl_pathv[lpc]);
                strcat(bits, "\n");
            }

            if (lpc < gl.gl_pathc) {
                strcat(bits, " ... and more! ...\n");
            }

            send_preview_data(preview_id, path, strlen(bits), bits);

            globfree(&gl);
            free(bits);
        }
    }
    else if (stat(path, &st) == -1) {
        char msg[1024];

        snprintf(msg, sizeof(msg),
                 "error: cannot open %s -- %s",
                 path,
                 strerror(errno));
        send_preview_error(preview_id, path, msg);
    } else if (S_ISREG(st.st_mode)) {
        size_t capacity = 1024 * 1024;
        char *bits = malloc(capacity);
        FILE *file;

        if ((file = fopen(path, "r")) == NULL) {
            char msg[1024];

            snprintf(msg, sizeof(msg),
                     "error: cannot open %s -- %s",
                     path,
                     strerror(errno));
            send_preview_error(preview_id, path, msg);
        } else {
            int line_count = 10;
            size_t offset = 0;
            char *line;

            while (line_count &&
                   (capacity - offset) > 1024 &&
                   (line = fgets(&bits[offset], capacity - offset, file)) != NULL) {
                offset += strlen(line);
                line_count -= 1;
            }

            fclose(file);

            send_preview_data(preview_id, path, offset, bits);
        }
        free(bits);
    } else if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);

        if (dir == NULL) {
            char msg[1024];

            snprintf(msg, sizeof(msg),
                     "error: unable to open directory -- %s",
                     path);
            send_preview_error(preview_id, path, msg);
        } else {
            char *bits = malloc(1024 * 1024);
            struct dirent *entry;
            int line_count = 10;

            bits[0] = '\0';
            while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 ||
                    strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                if (entry->d_type != DT_REG &&
                    entry->d_type != DT_DIR) {
                    continue;
                }
                if (line_count == 1) {
                    strcat(bits, " ... and more! ...\n");
                    break;
                }

                strcat(bits, entry->d_name);
                strcat(bits, "\n");

                line_count -= 1;
            }

            closedir(dir);

            send_preview_data(preview_id, path, strlen(bits), bits);

            free(bits);
        }
    } else {
        char msg[1024];

        snprintf(msg, sizeof(msg),
                 "error: path is not a file or directory -- %s",
                 path);
        send_preview_error(preview_id, path, msg);
    }
}

static
void handle_complete_path_request(const char *path)
{
    size_t path_len = strlen(path);
    char *glob_path = malloc(path_len + 3);
    struct stat st;

    strcpy(glob_path, path);
    fprintf(stderr, "complete path: %s\n", path);
    if (path[path_len - 1] != '/' &&
        stat(path, &st) == 0 &&
        S_ISDIR(st.st_mode)) {
        strcat(glob_path, "/");
    }
    if (path[path_len - 1] != '*') {
        strcat(glob_path, "*");
    }
    fprintf(stderr, "complete glob path: %s\n", glob_path);
    send_possible_paths(glob_path, 0);

    free(glob_path);
}

int main(int argc, char *argv[])
{
    int done = 0, timeout = 0;
    recv_state_t rstate = RS_PACKET_TYPE;

    // No need to leave ourselves around
    if (argc == 1) {
        unlink(argv[0]);
    }

    list_init(&client_path_list);

    {
        FILE *unameFile = popen("uname -mrsv", "r");

        if (unameFile != NULL) {
            char buffer[1024];

            fgets(buffer, sizeof(buffer), unameFile);
            char *bufend = buffer + strlen(buffer) - 1;
            while (isspace(*bufend)) {
                bufend -= 1;
            }
            *bufend = '\0';
            send_packet(STDOUT_FILENO,
                        TPT_ANNOUNCE,
                        TPPT_STRING, buffer,
                        TPPT_DONE);
            pclose(unameFile);
        }
    }

    while (!done) {
        struct pollfd pfds[1];

        pfds[0].fd = STDIN_FILENO;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;

        int ready_count = poll(pfds, 1, timeout);

        if (ready_count) {
            tailer_packet_type_t type;

            assert(rstate == RS_PACKET_TYPE);
            rstate = readall(rstate, STDIN_FILENO, &type, sizeof(type));
            if (rstate == RS_ERROR) {
                fprintf(stderr, "info: exiting...\n");
                done = 1;
            } else {
                switch (type) {
                    case TPT_OPEN_PATH:
                    case TPT_CLOSE_PATH:
                    case TPT_LOAD_PREVIEW:
                    case TPT_COMPLETE_PATH: {
                        char *path = readstr(&rstate, STDIN_FILENO);
                        int64_t preview_id = 0;

                        if (type == TPT_LOAD_PREVIEW) {
                            if (readint64(&rstate, STDIN_FILENO, &preview_id) == -1) {
                                done = 1;
                                break;
                            }
                        }
                        if (path == NULL) {
                            fprintf(stderr, "error: unable to get path to open\n");
                            done = 1;
                        } else if (read_payload_type(&rstate, STDIN_FILENO) != TPPT_DONE) {
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
                        } else if (type == TPT_CLOSE_PATH) {
                            struct client_path_state *cps = find_client_path_state(&client_path_list, path);

                            if (cps == NULL) {
                                fprintf(stderr, "warning: path is not open: %s\n", path);
                            } else {
                                list_remove(&cps->cps_node);
                                delete_client_path_state(cps);
                            }
                        } else if (type == TPT_LOAD_PREVIEW) {
                            handle_load_preview_request(path, preview_id);
                        } else if (type == TPT_COMPLETE_PATH) {
                            handle_complete_path_request(path);
                        }

                        free(path);
                        break;
                    }
                    case TPT_ACK_BLOCK:
                    case TPT_NEED_BLOCK: {
                        char *path = readstr(&rstate, STDIN_FILENO);
                        int64_t ack_offset = 0, ack_len = 0, client_size = 0;

                        if (type == TPT_ACK_BLOCK &&
                            (readint64(&rstate, STDIN_FILENO, &ack_offset) == -1 ||
                             readint64(&rstate, STDIN_FILENO, &ack_len) == -1 ||
                             readint64(&rstate, STDIN_FILENO, &client_size) == -1)) {
                            done = 1;
                            break;
                        }

                        // fprintf(stderr, "info: block packet path: %s\n", path);
                        if (path == NULL) {
                            fprintf(stderr, "error: unable to get block path\n");
                            done = 1;
                        } else if (read_payload_type(&rstate, STDIN_FILENO) != TPPT_DONE) {
                            fprintf(stderr, "error: invalid block packet\n");
                            done = 1;
                        } else {
                            struct client_path_state *cps = find_client_path_state(&client_path_list, path);

                            if (cps == NULL) {
                                fprintf(stderr, "warning: unknown path in block packet: %s\n", path);
                            } else if (type == TPT_NEED_BLOCK) {
                                fprintf(stderr, "info: client is tailing: %s\n", path);
                                cps->cps_client_state = CS_TAILING;
                            } else if (type == TPT_ACK_BLOCK) {
                                fprintf(stderr,
                                        "info: client acked: %s %lld\n",
                                        path,
                                        client_size);
                                if (ack_len == 0) {
                                    cps->cps_client_state = CS_TAILING;
                                } else {
                                    cps->cps_client_file_offset = ack_offset + ack_len;
                                    cps->cps_client_state = CS_INIT;
                                    cps->cps_client_file_size = client_size;
                                }
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
            if (poll_paths(&client_path_list, NULL)) {
                timeout = 0;
            } else {
                timeout = 1000;
            }
        }
    }

    return EXIT_SUCCESS;
}
