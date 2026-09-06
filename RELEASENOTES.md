### 0.2.2





* ``_mulle_mmap_allocator_attach`` now verifies the mapped region holds a valid mspace (matching base and capacity) before use, invoking the fail callback on mismatch instead of silently attaching to stale or ASLR-shifted memory
* ``_mulle_mmap_allocator_reset`` now calls the fail callback when ``create_mspace_with_base`` fails
* shared-memory tests updated to a ping-pong send/reply pattern (avoids FIFO overflow on silent write failure) and propagate child failure status



* add API Summary link to README Documentation section
* move TOC file to standard api/toc location

### 0.2.1

Various small improvements
