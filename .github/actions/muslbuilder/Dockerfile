FROM --platform=$TARGETPLATFORM alpine:latest AS depsbuilder

RUN apk add --no-cache \
    autoconf \
    automake \
    build-base \
    curl \
    ncurses \
    ncurses-terminfo \
    zip

RUN mkdir -p /fake.root /extract

ENV SQLITE_CFLAGS="\
    -DSQLITE_ENABLE_COLUMN_METADATA \
    -DSQLITE_SOUNDEX \
    -DSQLITE_ENABLE_DBSTAT_VTAB \
    -DSQLITE_ENABLE_API_ARMOR \
    -DSQLITE_ENABLE_JSON1 \
    -DSQLITE_ENABLE_UPDATE_DELETE_LIMIT \
    "

ENV NCURSES_FALLBACKS="\
ansi,\
cygwin,\
Eterm,\
Eterm-256color,\
gnome,\
gnome-256color,\
konsole,\
konsole-256color,\
linux,\
putty,\
rxvt,\
rxvt-256color,\
screen,\
screen-16color,\
screen-256color,\
tmux,\
tmux-256color,\
vt100,\
vt220,\
xterm,\
xterm-256color\
"

RUN curl -sSL https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz | tar xvzC /extract/ && \
    cd /extract/bzip* && \
    make install PREFIX=/fake.root && \
    cd /extract && rm -rf *

RUN curl -sSL https://github.com/lz4/lz4/releases/download/v1.10.0/lz4-1.10.0.tar.gz | tar xvzC /extract/ && \
    cd /extract/lz4* && \
    make install PREFIX=/fake.root && \
    cd /extract && rm -rf *

RUN curl -sSL https://github.com/facebook/zstd/releases/download/v1.5.6/zstd-1.5.6.tar.gz | tar xvzC /extract/ && \
    cd /extract/zstd* && \
    make install PREFIX=/fake.root && \
    cd /extract && rm -rf *

RUN curl -sSL https://www.zlib.net/zlib-1.3.1.tar.gz | tar xvzC /extract/ && \
    cd /extract/zlib-* && ./configure --prefix=/fake.root --static && \
    make -j2 && \
    make install && \
    cd /extract && rm -rf *

RUN curl -sSL https://github.com/tukaani-project/xz/releases/download/v5.6.3/xz-5.6.3.tar.gz | tar xvzC /extract/ && \
    cd /extract/xz-* && \
    ./configure --prefix=/fake.root \
        --disable-shared \
        "LDFLAGS=-L/fake.root/lib" \
        "CPPFLAGS=-I/fake.root/include" \
    && \
    make -j2 && \
    make install && \
    cd /extract && rm -rf *

RUN curl -sSL https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.44/pcre2-10.44.tar.gz | tar xvzC /extract/ && \
    cd /extract/pcre2-* && \
    ./configure --prefix=/fake.root \
        --enable-jit \
        --disable-shared \
     && \
     make -j2 && \
     make install && \
     cd /extract && rm -rf *

RUN curl -sSL https://ftp.gnu.org/gnu/ncurses/ncurses-6.4.tar.gz | tar xvzC /extract/ && \
    cd /extract/ncurses-* && \
    ./configure --prefix=/fake.root \
        --enable-ext-mouse \
        --enable-sigwinch \
        --enable-ext-colors \
        --enable-widec \
        --enable-termcap \
        --with-fallbacks=$NCURSES_FALLBACKS \
        --without-shared \
        --without-progs \
        --without-manpages \
        --without-tests \
    && \
    make -j2 install && \
    cd /extract && rm -rf *

RUN curl -sSL https://www.openssl.org/source/openssl-1.0.2n.tar.gz | tar xvzC /extract/ && \
    cd /extract/openssl-* && \
    ./config --prefix=/fake.root no-shared -fPIC && \
    make -j2 && \
    make install && \
    cd /extract && rm -rf *

RUN curl -sSL https://www.libssh2.org/download/libssh2-1.11.1.tar.gz | tar xvzC /extract/ && \
    cd /extract/libssh2-* && \
    ./configure --prefix=/fake.root \
        --with-libssl-prefix=/fake.root \
        --with-libz-prefix=/fake.root \
        --disable-shared \
        "CPPFLAGS=-I/fake.root/include" \
        "LDFLAGS=-L/fake.root/lib" && \
    make -j2 && \
    make install && \
    cd /extract && rm -rf *

RUN curl -sSL https://www.libarchive.org/downloads/libarchive-3.7.7.tar.gz | tar xvzC /extract/ && \
    cd /extract/libarchive-* && \
    ./configure --prefix=/fake.root \
        --disable-shared \
        "LDFLAGS=-L/fake.root/lib" \
        "CPPFLAGS=-I/fake.root/include" \
    && \
    make -j2 && \
    make install && \
    cd /extract && rm -rf *

RUN curl -sSL https://curl.se/download/curl-8.10.1.tar.gz | tar xvzC /extract/ && \
    cd /extract/curl-* && \
    ./configure --prefix=/fake.root \
        --disable-shared \
        --with-libssh2=/fake.root \
        --without-libpsl \
        --with-ssl=/fake.root \
        --with-zlib=/fake.root && \
    make -j2 && \
    make install && \
    cd /extract && rm -rf *

RUN curl -sSL https://www.sqlite.org/2024/sqlite-autoconf-3470000.tar.gz | tar xvzC /extract/ && \
    cd /extract/sqlite-* && \
    ./configure --disable-editline \
        --disable-shared \
        --prefix=/fake.root \
        CFLAGS="${SQLITE_CFLAGS}" \
    && \
    make -j2 && \
    make install && \
    cd /extract && rm -rf *

FROM --platform=$TARGETPLATFORM alpine:latest

COPY --from=depsbuilder /fake.root /fake.root

LABEL com.github.actions.name="C++ MUSL Builder Slim"
LABEL com.github.actions.description="Provides a C++ MUSL environment"
LABEL com.github.actions.icon="settings"
LABEL com.github.actions.color="orange"

RUN apk add --no-cache \
    autoconf \
    automake \
    build-base \
    cargo \
    git \
    libunistring-dev \
    libunistring-static \
    linux-headers \
    rust \
    zip

COPY entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
