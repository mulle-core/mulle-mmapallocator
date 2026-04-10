## 0.2.0







feature: attach allocator to existing shared memory and add debug dump

* add `_mulle_mmap_allocator_attach` and inline `mulle_mmap_allocator_attach` to map an allocator onto an existing shared-memory handle
* allocator records platform `shared_handle` and frees shared shared-memory on done; enable spin-lock based locking for shared mspaces
* add `_mulle_mmap_allocator_dump` to walk the mspace and hex-dump allocated chunks for debugging
* **BREAKING**: struct `mulle_mmap_allocator` layout changed (added `shared_handle)` — may affect ABI/static initializers
