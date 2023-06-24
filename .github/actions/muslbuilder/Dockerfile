FROM alpine:3.16

LABEL com.github.actions.name="C++ MUSL Builder Slim"
LABEL com.github.actions.description="Provides a C++ MUSL environment"
LABEL com.github.actions.icon="settings"
LABEL com.github.actions.color="orange"

RUN apk update && apk add --no-cache \
    build-base \
    binutils \
    m4 \
    git \
    cmake \
    make \
    libgcc \
    musl-dev \
    gcc \
    g++ \
    lz4 \
    lz4-dev \
    lz4-static \
    zip \
    zstd \
    zstd-dev \
    zstd-static \
    perl \
    autoconf \
    automake \
    elfutils \
    elfutils-dev \
    libelf-static \
    libexecinfo-dev \
    libexecinfo-static \
    libtool \
    libunwind \
    libunwind-dev \
    libunwind-static \
    linux-headers

ADD https://www.libarchive.org/downloads/libarchive-3.6.2.tar.gz /
ADD https://ftp.gnu.org/gnu/make/make-4.2.1.tar.gz /
ADD https://ftp.gnu.org/gnu/ncurses/ncurses-6.4.tar.gz /
ADD https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.42/pcre2-10.42.tar.gz /
ADD https://ftp.gnu.org/gnu/readline/readline-6.3.tar.gz /
ADD https://zlib.net/zlib-1.2.13.tar.gz /
ADD https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz /
ADD https://www.sqlite.org/2023/sqlite-autoconf-3420000.tar.gz /
ADD https://www.openssl.org/source/openssl-1.0.2n.tar.gz /
ADD https://www.libssh2.org/download/libssh2-1.11.0.tar.gz /
ADD https://curl.se/download/curl-8.1.2.tar.gz /
ADD https://tukaani.org/xz/xz-5.4.3.tar.gz /

RUN mkdir -p /fake.root /packages /extract
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

RUN cd /extract && for pkg in /*.tar.gz; do tar xvfz "$pkg"; done

RUN cd /extract/bzip2-1.0.8 && make install PREFIX=/fake.root && make clean

RUN cd /extract/zlib-* && ./configure --prefix=/fake.root && make -j2 && make install && make clean

RUN cd /extract/xz-* && \
 ./configure --prefix=/fake.root \
     --disable-shared \
     "LDFLAGS=-L/fake.root/lib" \
     "CPPFLAGS=-I/fake.root/include" \
      && \
 make -j2 && \
 make install && make clean

RUN cd /extract/libarchive-* && \
 ./configure --prefix=/fake.root \
     --disable-shared \
     "LDFLAGS=-L/fake.root/lib" \
     "CPPFLAGS=-I/fake.root/include" \
      && \
 make -j2 && \
 make install && make clean

RUN cd /extract/make-4.2.1 && ./configure --prefix=/fake.root && make -j2 && make install && make clean
RUN cd /extract/readline-* && ./configure --prefix=/fake.root && make -j2 && make install && make clean

RUN cd /extract/sqlite-* && \
 ./configure --disable-editline --prefix=/fake.root \
     CFLAGS="${SQLITE_CFLAGS}" \
     && \
 make -j2 && make install && make clean

RUN cd /extract/openssl-* && \
 ./config --prefix=/fake.root -fPIC && \
 make -j2 && \
 make install && make clean

RUN cd /extract/ncurses-* && \
 ./configure --prefix=/fake.root \
     --enable-ext-mouse \
     --enable-sigwinch \
     --with-default-terminfo-dir=/usr/share/terminfo \
     --enable-ext-colors \
     --enable-widec \
     --enable-termcap \
     --with-fallbacks=$NCURSES_FALLBACKS \
     && \
 make -j2 && make install && make clean

RUN cd /extract/pcre2-* && \
 ./configure --prefix=/fake.root \
     --enable-jit \
     && \
 make -j2 && make install && make clean

RUN cd /extract/libssh2-* && \
 ./configure --prefix=/fake.root \
     --with-libssl-prefix=/fake.root \
     --with-libz-prefix=/fake.root \
     "CPPFLAGS=-I/fake.root/include" \
     "LDFLAGS=-ldl -L/fake.root/lib" && \
 make -j2 && \
 make install && make clean

RUN cd /extract/curl-* && \
 ./configure --prefix=/fake.root \
     --with-libssh2=/fake.root \
     --with-ssl=/fake.root \
     --with-zlib=/fake.root \
     "LDFLAGS=-ldl" && \
 make -j2 && \
 make install && make clean

COPY entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
