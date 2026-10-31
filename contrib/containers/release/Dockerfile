# Release build environments. See README.md for which target builds where.

# ldid, for ad-hoc signing macOS binaries on a Linux host. Built on bookworm because it needs
# libplist 2.x; copy the result to the host (README.md), it is not used inside this image.
FROM debian:bookworm AS ldid
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential ca-certificates git libssl-dev libplist-dev pkg-config \
    && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 --branch v2.1.5-procursus7 https://github.com/ProcursusTeam/ldid.git /src/ldid \
    && make -C /src/ldid -j"$(nproc)" \
    && mkdir -p /out \
    && cp /src/ldid/ldid /out/ldid.bin \
    && cp -L /usr/lib/x86_64-linux-gnu/libplist-2.0.so.3 /out/

# The Linux release builder: x86_64-linux-gnu and aarch64-linux-gnu.
# bullseye's glibc 2.31 gives osmiumd a GLIBC_2.30 floor, so the binaries run on Ubuntu 20.04 and
# later and Debian 11 and later. A newer base raises the floor: libevent and expat link glibc's
# arc4random when their configure finds it (glibc 2.36+).
FROM debian:bullseye AS linux
# bullseye is end-of-life and its pools, security included, now live only on archive.debian.org.
RUN printf '%s\n' \
      'deb http://archive.debian.org/debian bullseye main' \
      'deb http://archive.debian.org/debian bullseye-updates main' \
      'deb http://archive.debian.org/debian-security bullseye-security main' \
      > /etc/apt/sources.list \
    && rm -f /etc/apt/sources.list.d/*
RUN apt-get -o Acquire::Check-Valid-Until=false update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      g++-aarch64-linux-gnu binutils-aarch64-linux-gnu \
      build-essential make automake autoconf libtool pkg-config bison flex \
      curl wget git ca-certificates patch xz-utils bzip2 unzip \
      python3 cmake ninja-build \
      bsdextrautils procps \
    && rm -rf /var/lib/apt/lists/*
# depends calls `python`.
RUN ln -sf /usr/bin/python3 /usr/local/bin/python
