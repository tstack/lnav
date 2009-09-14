#include "help.hh"

typedef enum {
    ME_FOO,
    ME_BAR,
} my_enum_t;

struct test {
    int foo;
    float other;
};

struct test bar;

my_enum_t blather;

static struct test baz;

const char help_text_start[] = "Bah!  No objcopy :(";
