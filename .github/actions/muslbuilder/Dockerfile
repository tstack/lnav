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
    zip \
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

COPY entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
