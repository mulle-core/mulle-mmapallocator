# mulle-mmapallocator

#### 🌍 mulle-mmapallocator a mulle-allocator that shared memory

mulle-mmapallocator can be used to create memory allocations shared across
process boundaries. Due to cross-platform considerations, the memory to be
shared with the other processes must be pre-allocated at initialization time.

Internally it uses mulle-dlmalloc's relocatable offset arena: allocator metadata
is stored as arena-relative offsets, so the region can be mapped anywhere. A
payload that stores raw pointers still requires every process to attach at the
same address; a payload that stores offsets can attach anywhere.

Every mode uses one fixed-capacity arena. There is no growth, so size the region
for the worst case up front.

mulle-mmapallocator can also be used to create a separate allocation
space, which can then easily be reclaimed by destroying the allocator.




| Release Version                                       | Release Notes  | AI Documentation
|-------------------------------------------------------|----------------|---------------
| ![Mulle kybernetiK tag](https://img.shields.io/github/tag/mulle-core/mulle-mmapallocator.svg) ![standard: C11](pix/c11.svg) ![apis: ansi](pix/apis-ansi.svg) [![Build Status](https://github.com/mulle-core/mulle-mmapallocator/workflows/CI/badge.svg)](//github.com/mulle-core/mulle-mmapallocator/actions) | [RELEASENOTES](RELEASENOTES.md) | [DeepWiki for mulle-mmapallocator](https://deepwiki.com/mulle-core/mulle-mmapallocator)




## Documentation & Guides

* [API Summary](asset/dox/api/toc)




### You are here

![Overview](overview.dot.svg)





## Add

mulle-mmapallocator is a component of the [mulle-core](//github.com/mulle-core/mulle-core) library. So in your code include the mulle-core umbrella header:

``` c
#include <mulle-core/mulle-core.h>
```

### Add mulle-core to a cmake project

Add mulle-core to your `CMakeLists.txt` with
[FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html):

``` cmake
include( FetchContent)

set( MULLE__CORE_TAG "master" CACHE STRING "branch or tag to fetch mulle-core from")

FetchContent_Declare( MULLE__CORE
   GIT_REPOSITORY https://github.com/mulle-core/mulle-core.git
   GIT_TAG        "${MULLE__CORE_TAG}")
FetchContent_MakeAvailable( MULLE__CORE)

target_link_libraries( ${PROJECT_NAME} PRIVATE mulle-core)
```


### Add mulle-core to a mulle-sde project

``` sh
mulle-sde add github:mulle-core/mulle-core
```

### Embed mulle-mmapallocator with clib

``` sh
clib install --out src mulle-core/mulle-mmapallocator
```

Append `src` to your include path (e.g. add `-isystem src`  to your `CFLAGS`)
and compile all the sources that were downloaded.

## Install

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
PREFIX_DIR="/usr/local"
cmake -B build                               \
      -DMULLE_SDK_PATH="${PREFIX_DIR}"       \
      -DCMAKE_INSTALL_PREFIX="${PREFIX_DIR}" \
      -DCMAKE_PREFIX_PATH="${PREFIX_DIR}"    \
      -DCMAKE_BUILD_TYPE=Release &&
cmake --build build --config Release &&
cmake --install build --config Release
```


## Author

[Nat!](https://mulle-kybernetik.com/weblog) for Mulle kybernetiK  



