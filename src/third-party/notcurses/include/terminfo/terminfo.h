#ifndef TERMINFO_H
#define TERMINFO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define TERMINFO_MAGIC       0x011A
#define TERMINFO_MAGIC_32BIT 0x021E

typedef struct {
    char* name;
    uint8_t* bools;
    int32_t* numbers;
    char** strings;
    int bool_count;
    int number_count;
    int string_count;

    uint8_t* ext_bools;
    int32_t* ext_numbers;
    char** ext_strings;
    int ext_bool_count;
    int ext_number_count;
    int ext_string_count;
    char** ext_names;
} Terminfo;

const char* terminfo_find_path_for_term(const char* term_name);

Terminfo* terminfo_load(const char* path);
Terminfo* terminfo_load_from_internal(const char* term_name);
Terminfo* terminfo_parse(const char* content, int size);
void terminfo_free(Terminfo* ti);

const char* terminfo_get_string(Terminfo* ti, int index);
int terminfo_get_number(Terminfo* ti, int index);
int terminfo_get_flag(Terminfo* ti, int index);

// Name-based access
const char* terminfo_get_string_by_name(Terminfo* ti, const char* name);
int terminfo_get_number_by_name(Terminfo* ti, const char* name);
int terminfo_get_flag_by_name(Terminfo* ti, const char* name);

typedef enum {
    TIPARM_INT,
    TIPARM_STR
} TiparmType;

typedef struct {
    TiparmType type;
    union {
        int i;
        const char* s;
    };
} TiparmValue;

// Core formatter
char* tiparm_s(const char* fmt, int argc, TiparmValue* argv);

// Helper constructors
static inline TiparmValue
tiparm_int(int value)
{
    TiparmValue v;
    v.type = TIPARM_INT;
    v.i = value;
    return v;
}

static inline TiparmValue
tiparm_str(const char* value)
{
    TiparmValue v;
    v.type = TIPARM_STR;
    v.s = value;
    return v;
}

#ifdef __cplusplus
}
#endif

#endif
