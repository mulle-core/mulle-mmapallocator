# mulle-mmapallocator Library Documentation for AI
<!-- Keywords: memory, mmap, allocator, shared, c -->
## 1. Introduction & Purpose

- mulle-mmapallocator provides an allocator implementation backed by a memory-mapped region. It supports private allocation spaces, shared (inter-process) allocations, and inflexible/static spaces that do not grow.
- Problems solved: create an isolated allocation arena that can be shared across processes or reclaimed as a unit.
- Key features: growth-capable mmap arenas, attach to existing shared handles, conversion helpers to/from generic mulle_allocator.
- Relationship: component of mulle-core; depends on mulle-mmap (for mmap/file handles) and mulle-allocator (allocator base API).

## 2. Key Concepts & Design Philosophy

- Allocator as an arena: the allocator wraps a memory-mapped region (mmap) and exposes a mulle_allocator-compatible interface.
- Modes via bitflags: runtime behavior controlled with bits (locking, shared, inflexible, dont_free, dont_scribble).
- Two lifecycles: create/initialize a new mmap-backed space or attach to an existing shared handle.
- Small header-only inline wrappers expose safe no-op checks (null-pointer tolerant) and cast helpers for interoperability.

## 3. Core API & Data Structures

### 3.1. [mulle-mmapallocator.h]

#### struct mulle_mmap_allocator
- Purpose: Represents a mmap-backed allocation arena implementing the MULLE_ALLOCATOR_BASE API.
- Key Fields (public/important):
  - MULLE_ALLOCATOR_BASE;  // base allocator fields (from mulle-allocator)
  - void *mspace;          // internal malloc/mspace pointer (private)
  - void *base;            // base address of mapped region (private)
  - size_t capacity;       // total capacity of the mmap region
  - int mode;              // bitflags controlling behavior
  - mulle_mmap_file_t shared_handle; // platform handle (fd/handle) for shared mappings

- Lifecycle Functions:
  - _mulle_mmap_allocator_init(struct mulle_mmap_allocator *p, size_t capacity, int mode)
    - Initialize a new allocator with the requested capacity and mode. Capacity may be 0 to let implementation pick defaults.
  - _mulle_mmap_allocator_attach(struct mulle_mmap_allocator *p, mulle_mmap_file_t handle, size_t capacity, void *base_address)
    - Attach allocator object to an existing mmap handle and base address (for shared use across processes).
  - _mulle_mmap_allocator_reset(struct mulle_mmap_allocator *p)
    - Reset the allocator internal state (makes it reusable).
  - _mulle_mmap_allocator_done(struct mulle_mmap_allocator *p)
    - Tear down the allocator and release/close the mapped region according to mode bits.
  - _mulle_mmap_allocator_dump(struct mulle_mmap_allocator *p)
    - Diagnostics: print/dump allocator internals (for debugging).

- Inline Wrappers:
  - mulle_mmap_allocator_init(...) — null-safe wrapper that calls _mulle_mmap_allocator_init
  - mulle_mmap_allocator_attach(...) — null-safe wrapper for _attach
  - mulle_mmap_allocator_reset(...) — null-safe wrapper for _reset
  - mulle_mmap_allocator_done(...) — null-safe wrapper for _done
  - mulle_mmap_allocator_as_allocator(...) — cast helper to treat as generic struct mulle_allocator*
  - mulle_allocator_as_mmap_allocator(...) — reverse cast helper

- Version helpers:
  - MULLE__MMAPALLOCATOR_VERSION constant and inline getters:
    - mulle_mmapallocator_get_version_major/minor/patch()
  - uint32_t mulle_mmapallocator_get_version(void) — exported symbol for C11 globals support.

### 3.2. [enum mulle_mmap_allocator_bit]
- mulle_mmap_allocator_default       = 0x0
- mulle_mmap_allocator_locking       = 0x1  // enable internal locking
- mulle_mmap_allocator_inflexible    = 0x2  // do not grow; fixed mapping
- mulle_mmap_allocator_shared        = 0x4  // use shared mapping semantics (inter-process)
- mulle_mmap_allocator_dont_free     = 0x8  // do not free underlying resources on done
- mulle_mmap_allocator_dont_scribble = 0x10 // avoid scribbling freed memory

## 4. Performance Characteristics

- Allocation speed: uses an internal mspace allocator; typical allocation/free are similar to malloc/mspace performance (amortized O(1) for allocation).
- Growth: if not inflexible, allocator may grow underlying mapping; growth behavior depends on platform and initial capacity.
- Memory overhead: capacity requested will be slightly larger than usable space; README notes ~2KB reserved overhead.
- Trade-offs: shared mode imposes platform-level constraints (handles, alignment). "inflexible" reduces complexity at cost of fixed capacity.
- Thread-safety: locking is optional. By default, locking may be off — enable mulle_mmap_allocator_locking to get internal synchronization. If not enabled, require external synchronization.

## 5. AI Usage Recommendations & Patterns

- Best practices:
  - Always use the provided lifecycle functions (mulle_mmap_allocator_init / attach / done / reset).
  - Use mulle_mmap_allocator_as_allocator() when passing to APIs expecting a generic mulle_allocator*.
  - For shared memory, call _attach with the platform handle and base address provided by the owner process.
- Common pitfalls:
  - Do not access private fields (mspace, base) directly — they are implementation detail and may change.
  - Be aware that capacity available to user allocations is ~2KB less than ordered — account for this when sizing.
  - If using shared mappings, ensure all processes agree on the base address and handle semantics.
- Idiomatic usage:
  - Initialize a stack-allocated struct mulle_mmap_allocator, then cast to mulle_allocator* for allocator APIs. Call done() when finished to reclaim the region.

## 6. Integration Examples

### Example 1: Creating a growable mmap-backed allocator and using as mulle_allocator

```c
#include <mulle-mmapallocator/mulle-mmapallocator.h>
#include <stdio.h>

int   main( int argc, char *argv[])
{
   struct mulle_mmap_allocator   mmap_allocator;
   struct mulle_allocator        *allocator;
   char                          *s;

   mulle_mmap_allocator_init( &mmap_allocator, 0, mulle_mmap_allocator_default);
   allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);
   {
      s = mulle_allocator_strdup( allocator, "VfL Bochum 1848");
      printf( "%s\n", s);
      mulle_allocator_free( allocator, s);
   }
   mulle_mmap_allocator_done( &mmap_allocator);

   return( 0);
}
```

### Example 2: Creating an inflexible (static) mmap allocator

```c
#include <mulle-mmapallocator/mulle-mmapallocator.h>
#include <stdio.h>

int   main( int argc, char *argv[])
{
   struct mulle_mmap_allocator   mmap_allocator;
   struct mulle_allocator        *allocator;
   char                          *s;

   mulle_mmap_allocator_init( &mmap_allocator, 0, mulle_mmap_allocator_inflexible);
   allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);
   {
      s = mulle_allocator_strdup( allocator, "VfL Bochum 1848");
      printf( "%s\n", s);
      mulle_allocator_free( allocator, s);
   }
   mulle_mmap_allocator_done( &mmap_allocator);

   return( 0);
}
```

### Example 3: Attach to an existing shared handle (conceptual)

- The owner process should create the mmap and publish a mulle_mmap_file_t handle and base address.
- A consumer process then calls _mulle_mmap_allocator_attach or the inline wrapper to attach to that handle and use the allocator as above.

## 7. Dependencies

- mulle-mmap (for memory mapped file support)
- mulle-allocator (allocator base API)

## 8. Shortcut

- If an existing TOC.md exists, compare differences since the last commit to update only delta. (Not applicable here; this file is generated from headers and tests.)


<!-- End of TOC.md for mulle-mmapallocator -->