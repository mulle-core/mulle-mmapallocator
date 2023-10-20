# mulle-mmapallocator

#### 🌍 mulle-mmapallocator a mulle-allocator that shared memory

mulle-mmapallocator can be used to create memory allocations shared across
process boundaries. Due to cross-platform considerations, the memory to be
shared with the other processes must be pre-allocated at initialization time.

mulle-mmapallocator can also be used to create a separate allocation
space, which can then easily be reclaimed by destroying the allocator.


| Release Version                                       | Release Notes
|-------------------------------------------------------|--------------
| ![Mulle kybernetiK tag](https://img.shields.io/github/tag/mulle-core/mulle-mmapallocator.svg?branch=release) [![Build Status](https://github.com/mulle-core/mulle-mmapallocator/workflows/CI/badge.svg?branch=release)](//github.com/mulle-core/mulle-mmapallocator/actions)| [RELEASENOTES](RELEASENOTES.md) |







## Add

**This project is a component of the [mulle-core](//github.com/mulle-core/mulle-core) library. As such you usually will *not* add or install it
individually, unless you specifically do not want to link against
`mulle-core`.**


### Add as an individual component

Use [mulle-sde](//github.com/mulle-sde) to add mulle-mmapallocator to your project:

``` sh
mulle-sde add github:mulle-core/mulle-mmapallocator
```

To only add the sources of mulle-mmapallocator with dependency
sources use [clib](https://github.com/clibs/clib):


``` sh
clib install --out src/mulle-core mulle-core/mulle-mmapallocator
```

Add `-isystem src/mulle-core` to your `CFLAGS` and compile all the sources that were downloaded with your project.


## Install

### Install with mulle-sde

Use [mulle-sde](//github.com/mulle-sde) to build and install mulle-mmapallocator and all dependencies:

``` sh
mulle-sde install --prefix /usr/local \
   https://github.com/mulle-core/mulle-mmapallocator/archive/latest.tar.gz
```

### Manual Installation

Install the requirements:

| Requirements                                 | Description
|----------------------------------------------|-----------------------
| [mulle-mmap](https://github.com/mulle-core/mulle-mmap)             | 🇧🇿 Memory mapped file access
| [mulle-allocator](https://github.com/mulle-c/mulle-allocator)             | 🔄 Flexible C memory allocation scheme

Download the latest [tar](https://github.com/mulle-core/mulle-mmapallocator/archive/refs/tags/latest.tar.gz) or [zip](https://github.com/mulle-core/mulle-mmapallocator/archive/refs/tags/latest.zip) archive and unpack it.

Install **mulle-mmapallocator** into `/usr/local` with [cmake](https://cmake.org):

``` sh
cmake -B build \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_PREFIX_PATH=/usr/local \
      -DCMAKE_BUILD_TYPE=Release &&
cmake --build build --config Release &&
cmake --install build --config Release
```

## Author

[Nat!](https://mulle-kybernetik.com/weblog) for Mulle kybernetiK


