FROM alpine:3.17

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

COPY entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
