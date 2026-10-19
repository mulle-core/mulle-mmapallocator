## 0.3.0







* ``_mulle_mmap_allocator_init_growable`` no longer page-rounds ``initial_capacity`.`
  Only the window is a mapping and has to be a whole number of pages; the first
  segment is a range inside it, so it is now aligned only to
  ``DLMALLOC_OFFSET_ALIGN`` and enlarged just enough to hold the arena header. On a
  16 KiB-page system an 8 KiB request was silently turned into 16 KiB, which
  broke `test/30-shared/grow.c` on Apple Silicon
* `test/30-shared/process.c` now attaches the child at an address of the system's
  choosing and resolves the child's allocation by arena offset instead of storing
  a raw pointer. A stored pointer forced the creator's address, which an exec'd
  child cannot obtain on macOS; the attach then failed and the parent printed
  `(null)`. The test also propagates a failed child instead of reading the region
  anyway.



* ``_mulle_mmap_allocator_init_growable`` no longer page-rounds ``initial_capacity`.`
  Only the window is a mapping and has to be a whole number of pages; the first
  segment is a range inside it, so it is now aligned only to
  ``DLMALLOC_OFFSET_ALIGN`` and enlarged just enough to hold the arena header. On a
  16 KiB-page system an 8 KiB request was silently turned into 16 KiB, which
  broke `test/30-shared/grow.c` on Apple Silicon
* `test/30-shared/process.c` now attaches the child at an address of the system's
  choosing and resolves the child's allocation by arena offset instead of storing
  a raw pointer. A stored pointer forced the creator's address, which an exec'd
  child cannot obtain on macOS; the attach then failed and the parent printed
  `(null)`. The test also propagates a failed child instead of reading the region
  anyway.


### 0.2.2





* ``_mulle_mmap_allocator_attach`` now verifies the mapped region holds a valid mspace (matching base and capacity) before use, invoking the fail callback on mismatch instead of silently attaching to stale or ASLR-shifted memory
* ``_mulle_mmap_allocator_reset`` now calls the fail callback when ``create_mspace_with_base`` fails
* shared-memory tests updated to a ping-pong send/reply pattern (avoids FIFO overflow on silent write failure) and propagate child failure status



* add API Summary link to README Documentation section
* move TOC file to standard api/toc location

### 0.2.1

Various small improvements
