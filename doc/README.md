Osmium Core
==========

This is the official reference wallet for Osmium digital currency and comprises the backbone of the Osmium peer-to-peer network. You can [download Osmium Core](https://www.osmium.space/downloads/) or [build it yourself](#building) using the guides below.

Running
---------------------
The following are some helpful notes on how to run Osmium Core on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/osmium-qt` (GUI) or
- `bin/osmiumd` (headless)

### Windows

Unpack the files into a directory, and then run osmium-qt.exe.

### macOS

Drag Osmium Core to your applications folder, and then run Osmium Core.

### Need Help?

* See the [Dash documentation](https://docs.dash.com)
for help and more information.
* Ask for help on [Osmium Discord](http://)

Building
---------------------
The following are developer notes on how to build Osmium Core on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [Dependencies](dependencies.md)
- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [NetBSD Build Notes](build-netbsd.md)
- [Android Build Notes](build-android.md)

Development
---------------------
The Osmium Core repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Productivity Notes](productivity.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- Source Code Documentation ***TODO***
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [JSON-RPC Interface](JSON-RPC-interface.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)

### Resources
* See the [Osmium Dash Documentation](https://dashcore.readme.io/)
  for technical specifications and implementation details.
* Discuss on the [Dash Forum](https://dash.space/forum), in the Development & Technical Discussion board.
* Discuss on [Dash Discord](http://stayosmiumy.com)
* Discuss on [Osmium Discord](http://)

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [osmium.conf Configuration File](osmium-conf.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [I2P Support](i2p.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [Managing Wallets](managing-wallets.md)
- [PSBT support](psbt.md)
- [Reduce Memory](reduce-memory.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [ZMQ](zmq.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
