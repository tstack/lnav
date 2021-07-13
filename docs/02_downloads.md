---
layout: page
title: Downloads
permalink: /downloads
---

The current **beta** release is [**v0.10.0-beta1**](https://github.com/tstack/lnav/releases/tag/v0.10.0-beta1).

The latest **stable** release is [**v{{ site.version }}**](https://github.com/tstack/lnav/releases/latest).

The following options are available for installing **lnav**:

## Linux

<!-- markdown-link-check-disable-next-line -->
Download a [statically linked 64-bit binary](https://github.com/tstack/lnav/releases/download/v{{site.version}}/lnav-{{site.version}}-musl-64bit.zip). 

Install from the [Snap Store](https://snapcraft.io/lnav):

```shell
% sudo snap install lnav
```

## MacOS

<!-- markdown-link-check-disable-next-line -->
Download a [statically linked 64-bit binary](https://github.com/tstack/lnav/releases/download/v{{site.version}}/lnav-0.9.0a-os-x.zip)

Install using [Homebrew](https://formulae.brew.sh/formula/lnav):

```shell
% brew install lnav
```

## Source

<!-- markdown-link-check-disable-next-line -->
Download the [source](https://github.com/tstack/lnav/releases/download/v{{site.version}}/lnav-{{site.version}}.tar.gz)
and install any dependencies.  The following commands will unpack the source
tar ball, configure the build for your system, build, and then install:

```shell
% tar xvfz lnav-{{site.version}}.tar.gz
% cd lnav-{{site.version}}
% ./configure
% make
% make install
```

### GitHub

If you would like to contribute to the development of lnav, visit our page on
[GitHub](https://github.com/tstack/lnav).
