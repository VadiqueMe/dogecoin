Mac OS X Build Instructions and Notes
====================================
The commands in this guide should be executed in a Terminal application.
The built-in one is located in `/Applications/Utilities/Terminal.app`

Preparation
-----------
Install the OS X command line tools:

`xcode-select --install`

When the popup appears, click `Install`

Then install [Homebrew](https://brew.sh)

Dependencies
----------------------

    brew install automake libtool boost --c++11 miniupnpc openssl pkg-config protobuf --c++11 qt5 libevent
    brew install berkeley-db # You need to make sure you install a version >= 5.1.29. Check the homebrew docs to find out how to install other versions

If you want to build the disk image with `make deploy` (.dmg / optional), you need RSVG

    brew install librsvg

NOTE: Building with Qt4 is still supported, however, could result in a broken UI. Building with Qt5 is recommended

Build Dogecoin Core
------------------------

1. Clone the dogecoin source code and cd into `dogecoin`

        git clone https://github.com/dogecoin/dogecoin
        cd dogecoin

2.  Build dogecoin:

    Configure and build the headless dogecoin binaries as well as the GUI (if Qt is found)

    You can disable the GUI build by passing `--without-gui` to configure

        ./autogen.sh
        ./configure
        make

3.  It is recommended to build and run the unit tests:

        make check

4.  You can also create a .dmg that contains the .app bundle (optional):

        make deploy

Running
-------

Dogecoin Core is now available at `./src/dogecoind`

Before running, it's recommended you create an RPC configuration file

    echo -e "rpcuser=dogecoinrpc\nrpcpassword=$(xxd -l 16 -p /dev/urandom)" > "/Users/${USER}/Library/Application Support/Dogecoin/dogecoin.conf"

    chmod 600 "/Users/${USER}/Library/Application Support/Dogecoin/dogecoin.conf"

The first time you run dogecoind, it will start downloading the blockchain. This process could take several hours and days

You can monitor the download process by looking at the debug.log file:

    tail -f $HOME/Library/Application\ Support/Dogecoin/debug.log

Other commands:
-------

    ./src/dogecoind -daemon # Starts the dogecoin daemon
    ./src/dogecoin-cli --help # Outputs a list of command-line options
    ./src/dogecoin-cli help # Outputs a list of RPC commands when the daemon is running
