#include "terminfo/terminfo.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "terminfo/capabilities.h"

#define PATH_SIZE 1024

static int
check_path_for_term(char* path,
                    const char* dir,
                    int dir_len,
                    const char* term_name)
{
    snprintf(
        path, PATH_SIZE, "%.*s/%02x/%s", dir_len, dir, term_name[0], term_name);
    if (access(path, R_OK) == 0) {
        return 1;
    }

    snprintf(
        path, PATH_SIZE, "%.*s/%c/%s", dir_len, dir, term_name[0], term_name);
    if (access(path, R_OK) == 0) {
        return 1;
    }

    return 0;
}

const char*
terminfo_find_path_for_term(const char* term_name)
{
    static const char* DEFAULT_DIRS[] = {
        "/usr/share/terminfo/",
        "/lib/terminfo/",
        "/usr/lib/terminfo/",
        "/etc/terminfo/",
        NULL,
    };

    if (!term_name || !term_name[0]) {
        return NULL;
    }

    char path[1024];
    const char* ti_dir = getenv("TERMINFO");
    if (ti_dir != NULL && ti_dir[0]) {
        if (check_path_for_term(path, ti_dir, strlen(ti_dir), term_name)) {
            return strdup(path);
        }
    }

    const char* dirs = getenv("TERMINFO_DIRS");

    if (!dirs || !dirs[0]) {
        dirs = "/usr/share/terminfo";
    }
    while (1) {
        const char* next_colon = strchr(dirs, ':');
        int dir_len = next_colon ? next_colon - dirs : strlen(dirs);

        if (check_path_for_term(path, dirs, dir_len, term_name)) {
            return strdup(path);
        }

        if (!next_colon) {
            break;
        }
        dirs = next_colon + 1;
    }

    for (int lpc = 0; DEFAULT_DIRS[lpc]; lpc++) {
        if (check_path_for_term(
                path, DEFAULT_DIRS[lpc], strlen(DEFAULT_DIRS[lpc]), term_name))
        {
            return strdup(path);
        }
    }

    return NULL;
}

Terminfo*
terminfo_load(const char* path)
{
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }

    struct stat st;

    fstat(fileno(fp), &st);
    char* content = malloc(st.st_size);

    Terminfo* retval = NULL;
    if (content != NULL) {
        fread(content, 1, st.st_size, fp);
        retval = terminfo_parse(content, st.st_size);
        free(content);
    }

    fclose(fp);
    return retval;
}

static int16_t
read_le16(const char* bits)
{
    return bits[0] | (bits[1] << 8);
}

static int32_t
read_le32(const char* bits)
{
    return bits[0] | (bits[1] << 8) | (bits[2] << 16) | (bits[3] << 24);
}

Terminfo*
terminfo_parse(const char* orig_content, int size)
{
    const char* content = orig_content;
    const struct {
        uint16_t magic;
        uint16_t names_size;
        uint16_t bools_count;
        uint16_t nums_count;
        uint16_t strs_count;
        uint16_t strtab_size;
    }* header = (void*) content;
    int num_size = 0;

    if (header->magic == TERMINFO_MAGIC) {
        num_size = 2;
    } else if (header->magic == TERMINFO_MAGIC_32BIT) {
        num_size = 4;
    } else {
        return NULL;
    }

    int remaining = size - sizeof(*header);
    content += sizeof(*header);

    Terminfo* retval = calloc(1, sizeof(Terminfo));

    if (header->names_size > remaining) {
        goto error;
    }
    retval->name = malloc(header->names_size);
    memcpy(retval->name, content, header->names_size);

    content += header->names_size;
    remaining -= header->names_size;

    if (header->bools_count > remaining) {
        goto error;
    }
    retval->bool_count = header->bools_count;
    retval->bools = malloc(retval->bool_count);
    memcpy(retval->bools, content, retval->bool_count);

    content += retval->bool_count;
    remaining -= retval->bool_count;
    if ((header->names_size + header->bools_count) % 2 != 0) {
        content += 1;
        remaining -= 1;
    }

    int std_num_size = header->nums_count * num_size;
    if (std_num_size > remaining) {
        goto error;
    }
    retval->number_count = header->nums_count;
    retval->numbers = malloc(header->nums_count * sizeof(int32_t));
    if (num_size == 2) {
        for (int lpc = 0; lpc < header->nums_count; lpc++) {
            retval->numbers[lpc] = read_le16(&content[lpc * num_size]);
        }
    } else {
        for (int lpc = 0; lpc < header->nums_count; lpc++) {
            retval->numbers[lpc] = read_le32(&content[lpc * num_size]);
        }
    }

    content += std_num_size;
    remaining -= std_num_size;

    int stroff_size = header->strs_count * sizeof(uint16_t);
    if (stroff_size + header->strtab_size > remaining) {
        goto error;
    }

    int16_t* str_offsets = (int16_t*) content;
    const char* strtab = content + stroff_size;

    retval->string_count = header->strs_count;
    retval->strings = calloc(retval->string_count, sizeof(const char*));
    for (int lpc = 0; lpc < header->strs_count; lpc++) {
        if (str_offsets[lpc] >= 0) {
            retval->strings[lpc] = strdup(&strtab[str_offsets[lpc]]);
        } else {
            retval->strings[lpc] = NULL;
        }
    }

    content += stroff_size + header->strtab_size;
    remaining -= stroff_size + header->strtab_size;
    if ((size - remaining) % 2 == 1) {
        content += 1;
        remaining -= 1;
    }
    struct {
        uint16_t bools_count;
        uint16_t nums_count;
        uint16_t strs_count;
        uint16_t strtab_size;
        uint16_t strtab_end;
    }* ext_header = (void*) content;

    if (remaining < sizeof(*ext_header)) {
        return retval;
    }

    content += sizeof(*ext_header);
    remaining -= sizeof(*ext_header);

    if (ext_header->bools_count > remaining) {
        goto error;
    }
    retval->ext_bool_count = ext_header->bools_count;
    retval->ext_bools = malloc(retval->ext_bool_count);
    memcpy(retval->ext_bools, content, retval->ext_bool_count);

    content += retval->ext_bool_count;
    remaining -= retval->ext_bool_count;
    if (ext_header->bools_count % 2 != 0) {
        content += 1;
        remaining -= 1;
    }

    int ext_num_size = ext_header->nums_count * num_size;
    if (ext_num_size > remaining) {
        goto error;
    }
    retval->ext_number_count = ext_header->nums_count;
    retval->ext_numbers = malloc(ext_header->nums_count * sizeof(int32_t));
    if (num_size == 2) {
        for (int lpc = 0; lpc < ext_header->nums_count; lpc++) {
            retval->ext_numbers[lpc] = read_le16(&content[lpc * num_size]);
        }
    } else {
        for (int lpc = 0; lpc < ext_header->nums_count; lpc++) {
            retval->ext_numbers[lpc] = read_le32(&content[lpc * num_size]);
        }
    }

    content += ext_num_size;
    remaining -= ext_num_size;

    int ext_stroff_size = (ext_header->bools_count + ext_header->nums_count
                           + ext_header->strs_count)
        * sizeof(uint16_t);
    if (ext_stroff_size + ext_header->strtab_size > remaining) {
        goto error;
    }

    int16_t* ext_str_offsets = (int16_t*) content;
    const char* ext_strtab = content + ext_stroff_size;

    retval->ext_string_count = ext_header->strs_count;
    retval->ext_strings = calloc(retval->ext_string_count, sizeof(const char*));
    for (int lpc = 0; lpc < ext_header->strs_count; lpc++) {
        if (ext_str_offsets[lpc] >= 0) {
            retval->ext_strings[lpc]
                = strdup(&ext_strtab[ext_str_offsets[lpc]]);
        } else {
            retval->ext_strings[lpc] = NULL;
        }
    }

    retval->ext_names = calloc(retval->ext_bool_count + retval->ext_number_count
                                   + retval->ext_string_count,
                               sizeof(char*));
    for (int lpc = 0; lpc < ext_header->bools_count + ext_header->nums_count
             + ext_header->strs_count;
         lpc++)
    {
        retval->ext_names[lpc] = strdup(
            &ext_strtab[ext_str_offsets[ext_header->strs_count + lpc]]);
    }

    return retval;

error:
    terminfo_free(retval);
    return NULL;
}

void
terminfo_free(Terminfo* ti)
{
    if (!ti)
        return;
    free(ti->name);
    free(ti->bools);
    free(ti->numbers);
    for (int lpc = 0; lpc < ti->string_count; lpc++) {
        free(ti->strings[lpc]);
    }
    free(ti->strings);
    free(ti->ext_bools);
    free(ti->ext_numbers);
    for (int lpc = 0; lpc < ti->ext_string_count; lpc++) {
        free(ti->ext_strings[lpc]);
    }
    free(ti->ext_strings);
    for (int lpc = 0;
         lpc < ti->ext_bool_count + ti->ext_number_count + ti->ext_string_count;
         lpc++)
    {
        free(ti->ext_names[lpc]);
    }
    free(ti->ext_names);
    free(ti);
}

const char*
terminfo_get_string(Terminfo* ti, int index)
{
    return (index < ti->string_count) ? ti->strings[index] : NULL;
}

int
terminfo_get_number(Terminfo* ti, int index)
{
    return (index < ti->number_count) ? ti->numbers[index] : -1;
}

int
terminfo_get_flag(Terminfo* ti, int index)
{
    return (index < ti->bool_count) ? ti->bools[index] : -1;
}

const char*
terminfo_get_string_by_name(Terminfo* ti, const char* name)
{
    int idx = terminfo_find_str_index(name);
    if (idx < 0) {
        int str_start_index = ti->ext_bool_count + ti->ext_number_count;

        for (int lpc = 0; lpc < ti->ext_string_count; lpc++) {
            if (strcmp(name, ti->ext_names[str_start_index + lpc]) == 0) {
                return ti->ext_strings[lpc];
            }
        }
        return NULL;
    }
    return terminfo_get_string(ti, idx);
}

int
terminfo_get_number_by_name(Terminfo* ti, const char* name)
{
    int idx = terminfo_find_num_index(name);
    if (idx < 0) {
        int num_start_index = ti->ext_bool_count;

        for (int lpc = 0; lpc < ti->ext_number_count; lpc++) {
            if (strcmp(name, ti->ext_names[num_start_index + lpc]) == 0) {
                return ti->ext_numbers[lpc];
            }
        }
        return -1;
    }
    return terminfo_get_number(ti, idx);
}

int
terminfo_get_flag_by_name(Terminfo* ti, const char* name)
{
    int idx = terminfo_find_bool_index(name);
    if (idx < 0) {
        for (int lpc = 0; lpc < ti->ext_bool_count; lpc++) {
            if (strcmp(name, ti->ext_names[lpc]) == 0) {
                return ti->ext_bools[lpc];
            }
        }
        return -1;
    }
    return terminfo_get_flag(ti, idx);
}

#define MAX_STACK 32

typedef enum {
    STK_INT,
    STK_STR
} StackType;

typedef struct {
    StackType type;
    union {
        int i;
        const char* s;
    };
} StackVal;

typedef struct {
    StackVal data[MAX_STACK];
    int top;
} Stack;

static void
push(Stack* s, StackVal val)
{
    if (s->top < MAX_STACK)
        s->data[s->top++] = val;
}

static StackVal
pop(Stack* s)
{
    if (s->top > 0)
        return s->data[--s->top];
    StackVal zero = {.type = STK_INT, .i = 0};
    return zero;
}

static StackVal
peek(Stack* s)
{
    return (s->top > 0) ? s->data[s->top - 1]
                        : (StackVal) {.type = STK_INT, .i = 0};
}

char*
tiparm_s(const char* fmt, int argc, TiparmValue* argv)
{
    size_t out_cap = 256;
    char* out = malloc(out_cap);
    size_t out_len = 0;

    Stack stack = {0};

    const char* p = fmt;

    int skip = 0;  // skip logic block
    int cond_level = 0;
    int exec = 1;  // whether we're in an active (true) branch
    int cond_execed = 0;

    while (*p) {
        if (*p == '%') {
            p++;
            switch (*p) {
                case '%':
                    if (exec && out_len + 1 < out_cap) {
                        out[out_len++] = '%';
                    }
                    p++;
                    break;
                case 'p': {
                    int idx = p[1] - '1';
                    if (idx >= 0 && idx < argc) {
                        if (argv[idx].type == TIPARM_INT) {
                            push(&stack,
                                 (StackVal) {STK_INT, .i = argv[idx].i});
                        } else {
                            push(&stack,
                                 (StackVal) {STK_STR, .s = argv[idx].s});
                        }
                    }
                    p += 2;
                    break;
                }
                case 'i':
                    if (argv[0].type == TIPARM_INT)
                        argv[0].i++;
                    if (argc > 1 && argv[1].type == TIPARM_INT)
                        argv[1].i++;
                    p++;
                    break;
                case '{': {
                    p++;
                    int val = 0;
                    int sign = 1;
                    if (*p == '-') {
                        sign = -1;
                        p++;
                    }
                    while (isdigit(*p)) {
                        val = val * 10 + (*p++ - '0');
                    }
                    if (*p == '}')
                        p++;
                    push(&stack, (StackVal) {STK_INT, .i = val * sign});
                    break;
                }
                case '+': {
                    StackVal b = pop(&stack), a = pop(&stack);
                    if (a.type == STK_INT && b.type == STK_INT)
                        push(&stack, (StackVal) {STK_INT, .i = a.i + b.i});
                    p++;
                    break;
                }
                case '-': {
                    StackVal b = pop(&stack), a = pop(&stack);
                    if (a.type == STK_INT && b.type == STK_INT)
                        push(&stack, (StackVal) {STK_INT, .i = a.i - b.i});
                    p++;
                    break;
                }
                case '=': {
                    StackVal b = pop(&stack), a = pop(&stack);
                    if (a.type == STK_INT && b.type == STK_INT)
                        push(&stack, (StackVal) {STK_INT, .i = (a.i == b.i)});
                    p++;
                    break;
                }
                case '<': {
                    StackVal b = pop(&stack), a = pop(&stack);
                    if (a.type == STK_INT && b.type == STK_INT)
                        push(&stack, (StackVal) {STK_INT, .i = (a.i < b.i)});
                    p++;
                    break;
                }
                case '>': {
                    StackVal b = pop(&stack), a = pop(&stack);
                    if (a.type == STK_INT && b.type == STK_INT)
                        push(&stack, (StackVal) {STK_INT, .i = (a.i > b.i)});
                    p++;
                    break;
                }
                case 'd': {
                    StackVal v = pop(&stack);
                    if (exec && v.type == STK_INT) {
                        char numbuf[32];
                        snprintf(numbuf, sizeof(numbuf), "%d", v.i);
                        size_t len = strlen(numbuf);
                        if (out_len + len >= out_cap) {
                            out_cap *= 2;
                            out = realloc(out, out_cap);
                        }
                        strcpy(&out[out_len], numbuf);
                        out_len += len;
                    }
                    p++;
                    break;
                }
                case 's': {
                    StackVal v = pop(&stack);
                    if (exec && v.type == STK_STR && v.s) {
                        size_t len = strlen(v.s);
                        if (out_len + len >= out_cap) {
                            while (out_len + len >= out_cap)
                                out_cap *= 2;
                            out = realloc(out, out_cap);
                        }
                        strcpy(&out[out_len], v.s);
                        out_len += len;
                    }
                    p++;
                    break;
                }
                case '?':  // start conditional
                    exec = 1;
                    cond_execed = 0;
                    cond_level++;
                    p++;
                    break;
                case 't': {
                    StackVal v = pop(&stack);
                    exec = exec && (v.type == STK_INT && v.i);
                    if (exec) {
                        cond_execed = 1;
                    }
                    p++;
                    break;
                }
                case 'e':  // else
                    if (cond_execed) {
                        exec = 0;
                    } else {
                        exec = !exec;
                    }
                    p++;
                    break;
                case ';':  // end if
                    cond_level--;
                    cond_execed = 0;
                    exec = 1;
                    p++;
                    break;
                default:
                    p++;
            }
        } else {
            if (exec) {
                if (out_len + 1 >= out_cap) {
                    out_cap *= 2;
                    out = realloc(out, out_cap);
                }
                out[out_len++] = *p;
            }
            p++;
        }
    }

    out[out_len] = '\0';
    return out;
}
