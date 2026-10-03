## 0.3.0



feature: add growable reserved-window allocator

* new `mulle_mmap_allocator_init_growable()` reserves a ceiling up front and expands the arena into it on demand, failing through the fail vector only once the window is used up
* new `mulle_mmap_allocator_grow()` plus capacity queries: `get_segment_count,` `get_current_capacity,` `get_grow_count,` `get_initial_capacity` and `get_locking`
* attach now reads the arena size from the header, so a child can map a growable region at any address and still resolve segments another process grew
* a growable arena stays a single segment for life; what grows is the current capacity, not the segment count
* **BREAKING** a private arena no longer takes the arena lock unless `mulle_mmap_allocator_locking` is set; `shared` still forces locking. An unlocked arena reached from two threads is a data race on the free lists
* the per-process arena view is now persistent instead of rebuilt per call, restoring the segment verification cache and removing a hot-path regression (up to 540x at 40000 allocations)





feature: rebuild mulle-mmapallocator on the relocatable offset arena


### 0.2.2





* ``_mulle_mmap_allocator_attach`` now verifies the mapped region holds a valid mspace (matching base and capacity) before use, invoking the fail callback on mismatch instead of silently attaching to stale or ASLR-shifted memory
* ``_mulle_mmap_allocator_reset`` now calls the fail callback when ``create_mspace_with_base`` fails
* shared-memory tests updated to a ping-pong send/reply pattern (avoids FIFO overflow on silent write failure) and propagate child failure status



* add API Summary link to README Documentation section
* move TOC file to standard api/toc location

### 0.2.1

Various small improvements
