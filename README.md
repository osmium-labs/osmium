Maximus Core staging tree
===========================

|CI|master|develop|
|-|-|-|
|GitHub|[![Build](https://github.com/maximus-chain/maximus/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/maximus-chain/maximus/actions/workflows/build.yml)|[![Build](https://github.com/maximus-chain/maximus/actions/workflows/build.yml/badge.svg?branch=develop)](https://github.com/maximus-chain/maximus/actions/workflows/build.yml)|

https://www.maximuschain.com

For an immediately usable, binary version of the Maximus Core software, see releases in our [GitHub](https://github.com/maximus-chain/maximus/releases) or pull our [Docker images](https://github.com/maximus-chain/maximus/pkgs/container/maximusd).

### Docker Images

| Tag | Architecture | Description |
|-----|--------------|-------------|
| `latest` | linux/amd64 | Stable release |
| `latest-arm64` | linux/arm64 | Stable for ARM devices |
| `develop` | linux/amd64 | Development build |
| `develop-arm64` | linux/arm64 | Development for ARM |

```bash
# Pull latest stable (Intel/AMD)
docker pull ghcr.io/maximus-chain/maximusd:latest

# Pull latest stable (ARM)
docker pull ghcr.io/maximus-chain/maximusd:latest-arm64
```

For more details, see [docker/README_DOCKER.md](docker/README_DOCKER.md).

Further information about Maximus Core is available in the [doc folder](/doc).

What is Maximus?
-------------

Maximus was born from a very simple idea, to promote and reward talent in the blockchain space. It is our goal to drive innovation and to give back what we create to the entire crypto community. We shall create a unique ecosystem that will have the capability to identify and fund innovative projects that can help the entire blockchain space.


For more information read the [Maximus whitepaper](https://maximuschain.com/#why_maxi).

License
-------

Maximus Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

Development Process
-------------------

The `master` branch is meant to be stable. Development is normally done in separate branches.
[Tags](https://github.com/maximus-chain/maximus/tags) are created to indicate new official,
stable release versions of Maximus Core.

The `develop` branch is regularly built (see doc/build-*.md for instructions) and tested, but is not guaranteed to be
completely stable.

The `Testnet` branch is where active day-to-day development currently takes place, including consensus,
build system, and dependency work. Changes are tested here before being merged upstream.

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md)
and useful hints for developers can be found in [doc/developer-notes.md](doc/developer-notes.md).

Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write [unit tests](src/test/README.md) for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run
(assuming they weren't disabled in configure) with: `make check`. Further details on running
and extending unit tests can be found in [/src/test/README.md](/src/test/README.md).

There are also [regression and integration tests](/test), written
in Python.
These tests can be run (if the [test dependencies](/test) are installed) with: `test/functional/test_runner.py`

GitHub Actions makes sure that every pull request is built for Windows, Linux, and macOS, and that unit/sanity tests are run automatically.

### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than the developer who wrote the
code. This is especially important for large or high-risk changes. It is useful
to add a test plan to the pull request description if testing the changes is
not straightforward.
