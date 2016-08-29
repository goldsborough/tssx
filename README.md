# TSSX

[![GitHub license](https://img.shields.io/github/license/mashape/apistatus.svg?style=flat-square)](http://goldsborough.mit-license.org)

TSSX stands for *transparent shared-memory socket exchange* and is a system-level C library that silently replaces domain socket communication with a custom shared memory data channel, promising performance improvements up to an order of magnitude.

## Usage

One of the core goals of TSSX is to be incredibly easy and hassle-free to integrate into your system. We use the [`LD_PRELOAD` trick](https://rafalcieslak.wordpress.com/2013/04/02/dynamic-linker-tricks-using-ld_preload-to-cheat-inject-features-and-investigate-programs/) to transparently overwrite system-call symbols with our own, using the dynamic linker. As such, if `./happy-banana-server` and `./happy-banana-client` are your executables using `write`/`read`, `send`/`recv` or similar system-calls to communicate over domains sockets, then the following lines will execute your application with TSSX:

```shell
$ LD_PRELOAD=$PWD/path/to/libtssx-server.so ./happy-banana-server
$ LD_PRELOAD=$PWD/path/to/libtssx-client.so ./happy-banana-client
```

where `libtssx-server.so` and `libtssx-client.so` are the result of compiling our library. And that is it! You don't have to recompile a single line, the dynamic linker does all the magic for  you.

## Compiling

The project can be built using [CMake](http://cmake.org) on Linux and OS X:

```shell
mkdir build
cd build
cmake ..
make
```

Which will compile the TSSX library into the `build/source/tssx` path. We also provide example programs in the `try/` folder, compiled into `build/try`, with appropriate run scripts (for convenience) in the `scripts/` directory (run them from `build/try`).

## Publication

We are working on a publication and will update this section accordingly in the near future.

## Authors

TSSX is developed by [Peter Goldsborough](@goldsborough), [Alexander van Renen](https://db.in.tum.de/~vanrenen/?lang=de) and [Viktor Leis](https://www-db.in.tum.de/~leis/) at the [Chair for Database Systems](https://db.in.tum.de) of [Technical University of Munich (TUM)](http://tum.de).
