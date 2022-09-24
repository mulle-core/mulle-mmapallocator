# mulle-mmapallocator

#### 🌍 mulle-mmapallocator a mulle-allocator that  memory mapped memory

mulle-mmapallocator can be used to create shared memory allocations. Due to
cross-platform considerations, the memory to be shared with the other
processes must be pre-allocated at initialization time.

mulle-mmapallocator can be also used to create a separate allocation
space, which can then be easily reclaimed by destroying the allocator.

## Example

``` c
struct mulle_mmap_allocator   mmap_allocator;
struct mulle_allocator        *allocator;
char                          *s;

mulle_mmap_allocator_init( &mmap_allocator, 0, mulle_mmap_allocator_default);
allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);

s = mulle_allocator_strdup( allocator, "VfL Bochum 1848");
printf( "%s\n", s);
mulle_allocator_free( allocator, s);

mulle_mmap_allocator_done( &mmap_allocator);
```


## mulle-sde

This is a [mulle-sde](//github.com/mulle-sde) project. mulle-sde combines
recursive package management with cross-platform builds via **cmake**:

| Action  | Command                               | Description   |
|---------|---------------------------------------|---------------|
| Build   | `mulle-sde craft [--release|--debug]` | Builds into local `kitchen` folder |
| Add     | `mulle-sde dependency add --c --github mulle-core mulle-mmapallocator` | Add | mulle-mmapallocator to another mulle-sde project as a dependency |
| Install | `mulle-sde install --prefix /usr/local | https://github.com/<|GITHUB_USER|>/mulle-mmapallocator.git` | Like `make install`|


### Manual Installation


Install the requirements:

|Requirements                                             | Description       |
|---------------------------------------------------------|-------------------|
|[mulle-allocator](//github.com/mulle-c/[mulle-allocator) | Some requirement  |

Install into `/usr/local`:

``` sh
cmake -B build \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_PREFIX_PATH=/usr/local \
      -DCMAKE_BUILD_TYPE=Release &&
cmake --build build --config Release &&
cmake --install build --config Release
```


<!--
extension : mulle-sde/sde
directory : demo/library
template  : .../README.md
Suppress this comment with `export MULLE_SDE_GENERATE_FILE_COMMENTS=NO`
-->
