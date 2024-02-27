---
layout: page
title: Downloads
permalink: /downloads
---

The latest **stable** release is [**v{{ site.version }}**](https://github.com/tstack/lnav/releases/latest).

The following options are available for installing **lnav**:

## Linux

<!-- markdown-link-check-disable-next-line -->
Download a [statically linked 64-bit binary](https://github.com/tstack/lnav/releases/download/v{{site.version}}/lnav-{{site.version}}-linux-musl-x86_64.zip).

Install from the [Snap Store](https://snapcraft.io/lnav):

```console
$ sudo snap install lnav
```

Install RPMs from [Package Cloud](https://packagecloud.io/tstack/lnav):

```console
$ curl -s https://packagecloud.io/install/repositories/tstack/lnav/script.rpm.sh | sudo bash
$ sudo yum install lnav
```

## MacOS

<!-- markdown-link-check-disable-next-line -->
Download a [statically linked 64-bit binary](https://github.com/tstack/lnav/releases/download/v{{site.version}}/lnav-{{site.version}}-x86_64-macos.zip)

Install using [Homebrew](https://formulae.brew.sh/formula/lnav):

```console
$ brew install lnav
```

## Source

<!-- markdown-link-check-disable-next-line -->
Download the [source](https://github.com/tstack/lnav/releases/download/v{{site.version}}/lnav-{{site.version}}.tar.gz)
and install any dependencies.  The following commands will unpack the source
tar ball, configure the build for your system, build, and then install:

```console
$ tar xvfz lnav-{{site.version}}.tar.gz
$ cd lnav-{{site.version}}
$ ./configure
$ make
$ make install
```

### GitHub

If you would like to contribute to the development of lnav, visit our page on
[GitHub](https://github.com/tstack/lnav).

# VSCode Extension

The [lnav VSCode Extension](https://marketplace.visualstudio.com/items?itemName=lnav.lnav)
can be used to add syntax highlighting to lnav scripts.

![Screenshot of an lnav script](/assets/images/lnav-vscode-extension.png)
