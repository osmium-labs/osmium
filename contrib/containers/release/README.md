# Release build environment

Release binaries are built from `depends`, for five hosts. Two build environments are needed:

| Host triplet | Builds where | Why |
|---|---|---|
| `x86_64-linux-gnu` | the `linux` image below | glibc floor |
| `aarch64-linux-gnu` | the `linux` image below | glibc floor |
| `x86_64-apple-darwin` | the build host | needs the host's clang/lld and the macOS SDK |
| `arm64-apple-darwin` | the build host | as above |
| `x86_64-w64-mingw32` | the build host | needs the POSIX-thread mingw-w64 toolchain |

Do not run the darwin or mingw `depends` builds inside the container.

## Linux

The image is Debian 11 (bullseye, glibc 2.31, gcc 10.2.1). Binaries built in it need at most
`GLIBC_2.30`, so they run on Ubuntu 20.04 and later and Debian 11 and later, the same floor as
Dash Core's release binaries. Building on a newer distribution raises the floor: `libevent` and
`expat` pick up glibc's `arc4random` (glibc 2.36) when their configure finds it.

Build the image:

```sh
docker build -t osmium-release-linux --target linux contrib/containers/release
```

Build from the root of a clean checkout, as your own user so nothing in the tree ends up owned
by root:

```sh
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD:/osmium" -w /osmium osmium-release-linux sh -c '
  make -C depends -j"$(nproc)" HOST=x86_64-linux-gnu &&
  ./autogen.sh &&
  CONFIG_SITE=$PWD/depends/x86_64-linux-gnu/share/config.site ./configure --prefix=/ &&
  make -j"$(nproc)"'
```

For ARM64 use `HOST=aarch64-linux-gnu` and its `config.site`. The container's two triplets get
their own `depends` prefixes, so they do not disturb a native development build in the same tree.

Check the floor:

```sh
objdump -T src/osmiumd | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
```

## macOS signing without a certificate

Osmium does not buy an Apple Developer ID, so macOS binaries are signed ad hoc. On a Linux build
host `make install` signs them with `ldid` (it prefers `codesign` when that is on `PATH`, i.e. on
a Mac). Build `ldid` with the `ldid` stage and copy it out:

```sh
docker build -t osmium-release-ldid --target ldid contrib/containers/release
id=$(docker create osmium-release-ldid)
mkdir -p ~/.local/lib/ldid
docker cp "$id:/out/." ~/.local/lib/ldid/
docker rm "$id"
printf '#!/bin/sh\nLD_LIBRARY_PATH="$HOME/.local/lib/ldid" exec "$HOME/.local/lib/ldid/ldid.bin" "$@"\n' > ~/.local/bin/ldid
chmod +x ~/.local/bin/ldid
```

The library is copied alongside the binary because Debian's `libplist-dev` has no static
library and older hosts do not ship `libplist-2.0.so.3`.

`ldid` signs Mach-O files but cannot seal an app bundle or write the designated requirement an
ad-hoc signature needs, so `codesign --verify` fails on a Linux-signed `Osmium-Qt.app`. For a
release, finish on a Mac with `codesign --force --deep --sign - Osmium-Qt.app`.

None of this is notarization. A copy downloaded through a browser is quarantined by Gatekeeper.
Users can clear it with `xattr -dr com.apple.quarantine Osmium-Qt.app`. Downloads made with
`curl` are not quarantined.
