
hunter_config(
    libpcre
    VERSION 8.41
    CMAKE_ARGS
    EXTRA_FLAGS=--enable-unicode-properties --enable-jit --enable-utf
)

hunter_config(
    readline
        VERSION 6.3
    CMAKE_ARGS
    EXTRA_FLAGS=CFLAGS=-Wno-implicit-function-declaration
)
