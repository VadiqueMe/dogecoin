# Dogecoin Core

Setup
---------------------
Dogecoin Core is the Dogecoin peer's node and it builds the backbone of the network. However, it downloads and stores the entire history of transactions (which is currently several GBs); depending on the speed of your computer and network connection, the synchronization process can take from a few hours to a day or more

Running
---------------------
The following are some helpful notes on how to run Dogecoin on your native platform

### Unix

Unpack the files into a directory and run:

- `bin/dogecoin-qt` (GUI) or
- `bin/dogecoind` (headless)

### Windows

Unpack the files into a directory, and then run dogecoin-qt.exe

### OS X

Drag Dogecoin-Core to your applications folder, and then run Dogecoin-Core

Building
---------------------
The following are developer notes on how to build Dogecoin on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [OS X Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [Gitian Building Guide](gitian-building.md)

Development
---------------------
The Dogecoin repo's [root README](/README.md) contains relevant information on the development process and automated testing

- [Developer Notes](developer-notes.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [Travis CI](travis-ci.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [ZMQ](zmq.md)

License
---------------------
Distributed under the [MIT software license](/COPYING)
This product includes software developed by the Bitcoin developers for use in [Dogecoin Core](https://www.bitcoin.org/)
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/)
This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard
