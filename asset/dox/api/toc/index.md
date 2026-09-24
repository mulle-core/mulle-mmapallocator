# mulle-mmapallocator Library Documentation for AI
<!-- Keywords: memory, mmap, allocator, shared, c -->
## 1. Introduction & Purpose

- mulle-mmapallocator provides an allocator implementation backed by a memory-mapped region. It supports private allocation spaces and shared (inter-process) allocations.
- Problems solved: create an isolated allocation arena that can be shared across processes or reclaimed as a unit.
- Key features: a fixed-capacity mmap arena, attach to an existing shared handle, conversion helpers to/from generic mulle_allocator.
- The allocator is mulle-dlmalloc's relocatable offset arena: all allocator metadata lives in the mapping as arena-relative offsets, so the region may be mapped at a different address in every process. It is a single fixed-capacity region with no growth, no trimming and no multiple segments.
- Relationship: component of mulle-core; depends on mulle-mmap (for mmap/file handles) and mulle-allocator (allocator base API), and embeds mulle-dlmalloc.

## 2. Key Concepts & Design Philosophy

- Allocator as an arena: the allocator wraps a memory-mapped region (mmap) and exposes a mulle_allocator-compatible interface.
- Offsets underneath: the embedded offset arena stores free-list links and block back-distances as offsets, never as process-local addresses. The generic interface still passes out process-local `void *` values; a pointer maps to its arena offset by subtracting `base`, and back by adding it.
- Raw pointers in payloads are not relocatable: if user data stored in the mapping contains `void *` values (the common case for convenience objects), every process must attach at the creator's address. Only payloads that store offsets themselves may attach anywhere. Pass the creator's address as the `base_address` mapping hint.
- Modes via bitflags: runtime behavior controlled with bits (locking, shared, inflexible, dont_free, dont_scribble).
- Two lifecycles: create/initialize a new mmap-backed space, or attach to an existing shared handle.
- Small header-only inline wrappers expose safe no-op checks (null-pointer tolerant) and cast helpers for interoperability.

## 3. Core API & Data Structures

### 3.1. [mulle-mmapallocator.h]

#### struct mulle_mmap_allocator
- Purpose: Represents a mmap-backed allocation arena implementing the MULLE_ALLOCATOR_BASE API.
- Key Fields (public/important):
  - MULLE_ALLOCATOR_BASE;  // base allocator fields (from mulle-allocator)
  - void *base;            // base address of the mapped region (private)
  - size_t capacity;       // total capacity of the mmap region
  - int mode;              // bitflags controlling behavior
  - mulle_mmap_file_t shared_handle; // platform handle (fd/handle) for shared mappings

- Lifecycle Functions:
  - _mulle_mmap_allocator_init(struct mulle_mmap_allocator *p, size_t capacity, int mode)
    - Initialize a new allocator with the requested capacity and mode. Capacity is rounded up to a whole number of pages, minimum one page.
    - The offset arena is always a fixed region, so the implementation always maps its own memory: shared memory when `mulle_mmap_allocator_shared` is set (which also forces `mulle_mmap_allocator_inflexible | mulle_mmap_allocator_locking`), otherwise anonymous pages. It initializes the arena once and attaches to it.
    - Calls the fail callback (via `p->fail`) if the mapping, arena initialization or attach fails.
  - _mulle_mmap_allocator_attach(struct mulle_mmap_allocator *p, mulle_mmap_file_t handle, size_t capacity, void *base_address)
    - Attach allocator object to an existing mmap handle. Forces mode to `mulle_mmap_allocator_shared | mulle_mmap_allocator_inflexible | mulle_mmap_allocator_dont_free`, so `_done` will not free the underlying mapping.
    - `base_address` is passed through as the mapping hint. Supply the owner's address when shared payloads contain raw pointers; pass NULL to map anywhere when only offsets are shared.
    - Validates the mapped region by checking the versioned arena header and configuration word, then walking the arena once to confirm consistency. A stale or zeroed region (or an incompatible configuration) invokes the fail callback instead of attaching. It never reinitializes shared data.
  - _mulle_mmap_allocator_reset(struct mulle_mmap_allocator *p)
    - Rebuild the arena in place via `dlmalloc_offset_arena_init` and attach again. This discards every existing allocation, so it requires exclusive access; calls the fail callback on failure.
  - _mulle_mmap_allocator_done(struct mulle_mmap_allocator *p)
    - Tear down the allocator and release/close the mapping according to mode bits. There is no allocator state to destroy besides the mapping.
  - _mulle_mmap_allocator_dump(struct mulle_mmap_allocator *p)
    - Diagnostics: prints the arena header and block tiling to stderr. An in-use block reports its usable payload size, a free block its full block size.

- Inline Wrappers:
  - mulle_mmap_allocator_init(...) — null-safe wrapper that calls _mulle_mmap_allocator_init
  - mulle_mmap_allocator_attach(...) — null-safe wrapper for _attach
  - mulle_mmap_allocator_reset(...) — null-safe wrapper for _reset
  - mulle_mmap_allocator_done(...) — null-safe wrapper for _done
  - mulle_mmap_allocator_as_allocator(...) — cast helper to treat as generic struct mulle_allocator*
  - mulle_allocator_as_mmap_allocator(...) — reverse cast helper

- Version helpers:
  - `MULLE__MMAPALLOCATOR_VERSION` constant (current development version 0.2.2) and inline getters:
    - mulle_mmapallocator_get_version_major/minor/patch()
  - uint32_t mulle_mmapallocator_get_version(void) — exported symbol for C11 globals support.

### 3.2. [enum mulle_mmap_allocator_bit]
- mulle_mmap_allocator_default       = 0x0
- mulle_mmap_allocator_locking       = 0x1  // enable internal locking (the offset arena always locks; bit retained for compatibility)
- mulle_mmap_allocator_inflexible    = 0x2  // do not grow (every mode is fixed now; bit retained for compatibility)
- mulle_mmap_allocator_shared        = 0x4  // use shared mapping semantics (inter-process)
- mulle_mmap_allocator_dont_free     = 0x8  // do not free underlying resources on done
- mulle_mmap_allocator_dont_scribble = 0x10 // avoid scribbling freed memory

## 4. Performance Characteristics

- Allocation speed: uses the embedded offset arena's segregated free lists; allocation/free are bounded by the size-class structure (practical O(1)).
- Growth: none. Every mode is now a fixed-capacity region. Ask for the capacity you need up front.
- Memory overhead: the arena header, the free-list table and a 16-byte per-block header; README notes ~2KB reserved overhead.
- Trade-offs: shared mode imposes platform-level constraints (handles, alignment) and a fixed capacity reduces complexity.
- Thread-safety: the offset arena takes a process-shared atomic lock around every operation, so use from several threads or processes is serialized. The lock has no owner tracking: a process that dies while holding it wedges the arena, and an interrupted operation can leave metadata inconsistent. There is no crash recovery; discard or reinitialize instead.

## 5. AI Usage Recommendations & Patterns

- Best practices:
  - Always use the provided lifecycle functions (mulle_mmap_allocator_init / attach / done / reset).
  - Use mulle_mmap_allocator_as_allocator() when passing to APIs expecting a generic mulle_allocator*.
  - For shared memory, call _attach with the platform handle and, if payloads hold raw pointers, the owner's base address.
- Common pitfalls:
  - Do not access private fields (base, capacity) directly — they are implementation detail and may change.
  - Be aware that capacity available to user allocations is less than ordered — account for this when sizing.
  - A `void *` handed out by the generic allocator is valid only in the calling process and only while the object is live. Storing such a pointer in shared memory forces all processes to attach at the same address. Store offsets instead if you need to attach anywhere.
  - After `_attach`, mode is forced to `shared | inflexible | dont_free`: `_done` will NOT munmap/unlink the shared mapping for that instance.
  - Treat the mapping as writable by any attaching process: only share it between processes that trust each other.
- Idiomatic usage:
  - Initialize a stack-allocated struct mulle_mmap_allocator, then cast to mulle_allocator* for allocator APIs. Call done() when finished to reclaim the region.

## 6. Integration Examples

### Example 1: Creating a private mmap-backed allocator and using as mulle_allocator

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

- The owner process creates the mapping with `mulle_mmap_allocator_shared`, then publishes a mulle_mmap_file_t handle and the capacity.
- A consumer process calls _mulle_mmap_allocator_attach (or the inline wrapper) with that handle. Pass the owner's base address when shared payloads contain raw pointers; pass NULL only when payloads store offsets.

## 7. Dependencies

- mulle-mmap (for memory mapped file support)
- mulle-allocator (allocator base API)
- mulle-dlmalloc (embedded; provides the relocatable offset arena)

## 8. Shortcut

- The allocator internals were reworked from dlmalloc/mspace to mulle-dlmalloc's relocatable offset arena: `struct mulle_mmap_allocator` no longer stores an `mspace`, attach no longer compares `seg.base`, and `_reset` reinitializes the arena in place.

<!-- End of TOC.md for mulle-mmapallocator -->