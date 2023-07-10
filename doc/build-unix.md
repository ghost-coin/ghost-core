UNIX BUILD NOTES
====================
How to build Ghost Core in Unix.

Ghost Core can be built as a statically linked package from natively compiled [depends](/depends/README.md).
This ensures that proper dependency versions are packed into distribution-agnostic executable.
Dynamically linked package, on the other hand, must rely on libraries from your distributions' reposotory. 
In this case presence of proper dependency versions is not guaranteed. 

Dependencies
---------------------

These dependencies are required:

 Library     | Purpose          | Description
 ------------|------------------|----------------------
 libboost    | Utility          | Library for threading, data structures, etc
 libevent    | Networking       | OS independent asynchronous networking

Optional dependencies:

 Library     | Purpose          | Description
 ------------|------------------|----------------------
 miniupnpc   | UPnP Support     | Firewall-jumping support
 libdb4.8    | Berkeley DB      | Wallet storage (only needed when wallet enabled)
 qt          | GUI              | GUI toolkit (only needed when GUI enabled)
 libqrencode | QR codes in GUI  | Optional for generating QR codes (only needed when GUI enabled)
 univalue    | Utility          | JSON parsing and encoding (bundled version will be used unless --with-system-univalue passed to configure)
 libzmq3     | ZMQ notification | Optional, allows generating ZMQ notifications (requires ZMQ version >= 4.0.0)
 sqlite3     | SQLite DB        | Optional, wallet storage (only needed when wallet enabled)
 protobuf    | USB Devices      | Optional, Data interchange format (only needed when usbdevice enabled)
 hidapi      | USB Devices      | Optional, USB interface wrapper (only needed when usbdevice enabled)

For the versions used, see [dependencies.md](dependencies.md)

Memory Requirements
--------------------

C++ compilers are memory-hungry. It is recommended to have at least 1.5 GB of
memory available when compiling Ghost Core. On systems with less, gcc can be
tuned to conserve memory with additional CXXFLAGS:

    ./configure CXXFLAGS="--param ggc-min-expand=1 --param ggc-min-heapsize=32768"

Alternatively, or in addition, debugging information can be skipped for compilation. The default compile flags are
`-g -O2`, and can be changed with:

    ./configure CXXFLAGS="-O2"

Finally, clang (often less resource hungry) can be used instead of gcc, which is used by default:

    ./configure CXX=clang++ CC=clang

See [*Configure Script*](/doc/build-unix.md#configure-script)

## Linux Distribution Specific Instructions

### Ubuntu & Debian

#### Dependency Build Instructions

Build requirements:

    sudo apt-get install -y git make automake cmake curl g++-multilib libtool binutils-gold bsdmainutils pkg-config python3 patch build-essential libtool autotools-dev

Additional requirements for dynamically linked binaries:

	sudo apt-get install libevent-dev libboost-system-dev libboost-filesystem-dev libboost-test-dev libboost-thread-dev

**Note**: When building dynamically linked binaries, BerkeleyDB 4.8 is required for the wallet.
Ubuntu and Debian do not have BerkeleyDB 4.8 in their repositories. Their `libdb-dev` and `libdb++-dev` are
BerkeleyDB 5.1 or later. Installing these will break compatibility of wallet files with the officially distributed executables,
that are based on BerkeleyDB 4.8. If you do not care about wallet compatibility,
pass <br />  `--with-incompatible-bdb` to the configure script.

SQLite is required for the wallet:

    sudo apt install libsqlite3-dev

To build Ghost Core without wallet, see [*Disable-wallet mode*](/doc/build-unix.md#disable-wallet-mode)

Optional (see `--with-miniupnpc` and `--enable-upnp-default`):

    sudo apt-get install libminiupnpc-dev

ZMQ dependencies (provides ZMQ API):

    sudo apt-get install libzmq3-dev

GUI dependencies:

If you want to build ghost-qt, make sure that the required packages for Qt development
are installed. Qt 5 is necessary to build the GUI.
To build without GUI pass `--without-gui`.

To build with Qt 5 you need the following:

    sudo apt-get install libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools

libqrencode (optional) can be installed with:

    sudo apt-get install libqrencode-dev

USB Device dependencies:

To build with USB Device support you need the following:

	sudo apt-get install libprotobuf-dev protobuf-compiler libhidapi-dev

Once these are installed, they will be found by configure and a ghost-qt executable will be
built by default.

### Fedora

#### Dependency Build Instructions

Build requirements:

    sudo dnf install gcc-c++ libtool make autoconf automake libevent-devel boost-devel libdb4-devel libdb4-cxx-devel python3

Optional (see `--with-miniupnpc` and `--enable-upnp-default`):

    sudo dnf install miniupnpc-devel

ZMQ dependencies (provides ZMQ API):

    sudo dnf install zeromq-devel

To build with Qt 5 you need the following:

    sudo dnf install qt5-qttools-devel qt5-qtbase-devel

libqrencode (optional) can be installed with:

    sudo dnf install qrencode-devel

SQLite can be installed with:

    sudo dnf install sqlite-devel

protobuf (optional) can be installed with:

    sudo dnf install protobuf-devel

### Gentoo Linux

#### Dependency Build Instructions

Build requirements:

    sudo emerge --ask --verbose dev-vcs/git
	sudo emerge --ask --verbose --onlydeps net-p2p/bitcoin-qt

**Note**: This is the most certain way to fetch and assemble all the necessary tools for building both 
statically and dynamically linked Ghost Core. 
However, with `--onlydeps` the dependencies are installed as orphaned packages. 

(optional) To prevent tools from removing upon dependency cleaning, record the corresponding orphaned packages into the @world set:

	sudo emerge --noreplace <package-name>

**Note**: To get Ghost Core running on musl-based Gentoo systems, make sure to set LC_ALL="C" environment variable.

Configure Script
--------------------------
To generate the configure script:

	./autogen.sh
	
A list of all configure flags can be displayed with:

    ./configure --help

**Note**: Always use absolute paths when passing parameters to configure script. 
For example, when specifying the path to self-compiled dependencies:

	./configure --prefix=$PWD/depends/x86_64-pc-linux-gnu

Here `--prefix` must be an absolute path - it is defined by $PWD which ensures the usage of the absolute path.

miniupnpc
---------

[miniupnpc](https://miniupnp.tuxfamily.org) may be used for UPnP port mapping.  It can be downloaded from [here](
https://miniupnp.tuxfamily.org/files/).  UPnP support is compiled in and
turned off by default.  See the configure options for upnp behavior desired:

	--without-miniupnpc      No UPnP support miniupnp not required
	--disable-upnp-default   (the default) UPnP support turned off by default at runtime
	--enable-upnp-default    UPnP support turned on by default at runtime

Berkeley DB
-----------

It is recommended to use Berkeley DB 4.8. If you have to build it yourself,
you can use [the installation script included in contrib/](/contrib/install_db4.sh)
like so:

```shell
./contrib/install_db4.sh `pwd`
```

from the root of the repository.

**Note**: You only need Berkeley DB if the wallet is enabled (see [*Disable-wallet mode*](/doc/build-unix.md#disable-wallet-mode)).

Boost
-----
If you need to build Boost yourself:

	sudo su
	./bootstrap.sh
	./bjam install

Security
--------
To help make your Ghost Core installation more secure by making certain attacks impossible to
exploit even if a vulnerability is found, binaries are hardened by default.
This can be disabled with:

Hardening Flags:

	./configure --enable-hardening
	./configure --disable-hardening


Hardening enables the following features:
* _Position Independent Executable_: Build position independent code to take advantage of Address Space Layout Randomization
    offered by some kernels. Attackers who can cause execution of code at an arbitrary memory
    location are thwarted if they don't know where anything useful is located.
    The stack and heap are randomly located by default, but this allows the code section to be
    randomly located as well.

    On an AMD64 processor where a library was not compiled with -fPIC, this will cause an error
    such as: "relocation R_X86_64_32 against `......` can not be used when making a shared object;"

    To test that you have built PIE executable, install scanelf, part of paxutils, and use:

        scanelf -e ./ghost-qt

    The output should contain:

     TYPE
    ET_DYN

* _Non-executable Stack_: If the stack is executable then trivial stack-based buffer overflow exploits are possible if
    vulnerable buffers are found. By default, Ghost Core should be built with a non-executable stack,
    but if one of the libraries it uses asks for an executable stack or someone makes a mistake
    and uses a compiler extension which requires an executable stack, it will silently build an
    executable without the non-executable stack protection.

    To verify that the stack is non-executable after compiling use:
    `scanelf -e ./ghost-qt`

    The output should contain:
	STK/REL/PTL
	RW- R-- RW-

    The STK RW- means that the stack is readable and writeable but not executable.

Disable-wallet mode
--------------------
When the intention is to run only a P2P node without a wallet, Ghost Core may be compiled in
disable-wallet mode with:

    ./configure --disable-wallet

In this case there is no dependency on Berkeley DB 4.8 and SQLite.

To Build statically linked executables for `x86_64-pc-linux-gnu`
---------------------

To build statically linked Ghost Core, [depends](/depends/README.md) must be compiled in the first place.

	git clone https://github.com/ghost-coin/ghost-core.git
	cd ghost-core/depends
	make download-linux
	make HOST=x86_64-pc-linux-gnu
	cd ..
	./autogen.sh
	./configure --prefix=$PWD/depends/x86_64-pc-linux-gnu
	make

To Build dynamically linked executables
---------------------

	git clone https://github.com/ghost-coin/ghost-core.git
    cd ghost-core
	./autogen.sh
	./configure
	make
	make install # optional

This will build ghost-qt as well, if otherwise is not specified.

**Note**: The release is built with GCC and then "strip ghostd" to strip the debug
symbols, which reduces the executable size by about 90%.

Setup and Build Example: Ubuntu 20.04 x86_64
-----------------------------------

The steps necessary to build fully featured statically linked Ghost Core executables:

	sudo apt update && sudo apt full-upgrade -y
	sudo apt install -y git make automake cmake curl g++-multilib libtool binutils-gold bsdmainutils pkg-config python3 patch build-essential libtool autotools-dev
	git clone https://github.com/ghost-coin/ghost-core.git
	cd ghost-core/depends
	make -j$(nproc) HOST=x86_64-pc-linux-gnu
	cd ..
	./autogen.sh
	./configure --prefix=$PWD/depends/x86_64-pc-linux-gnu
	make -j$(nproc)

ARM Cross-compilation
-------------------
These steps can be performed on a Ubuntu VM from a previous example. The depends system
will also work on other Linux distributions, however the commands for
installing the toolchain will be different.

Make sure you install the build requirements mentioned above.
Then, install the toolchain and curl:

    sudo apt-get install g++-arm-linux-gnueabihf curl

To build executables for ARM:

    cd ghost-core/depends
    make HOST=arm-linux-gnueabihf NO_QT=1
    cd ..
    ./autogen.sh
    ./configure --prefix=$PWD/depends/arm-linux-gnueabihf --enable-glibc-back-compat --enable-reduce-exports LDFLAGS=-static-libstdc++
    make


For further documentation on the depends system see [README.md](../depends/README.md) in the depends directory.
