# mulle-mmapallocator design

mulle-mmapallocator is a `mulle_allocator` backed by one memory-mapped region.
The region can be private to a process or shared between processes, and it can be
reclaimed as a unit when it is done with. It embeds mulle-dlmalloc's relocatable
offset arena, so all allocator metadata lives *inside* the region as
arena-relative offsets and the region may be mapped at a different address in
every process that attaches it.

The API reference is [asset/dox/api/toc](asset/dox/api/toc); this file is the
design behind it.

## One region, offsets underneath

"The allocator" *is* the mapping at `base`. There is no allocator state outside
it: the header, the free-list bucket table, the segment directory and the blocks
are all in the region, and freeing a pointer that another process allocated is
ordinary, because the free lists are in the shared pages.

The only process-local piece is the `dlmalloc_offset_view` — `base`, `capacity`,
`epoch`, `segment_count`, the `local_base[]` table, the bits recording which
segments this process has already checked, and the callbacks. Each process holds
exactly one, in `struct mulle_mmap_allocator`, for as long as the arena is in use.
It is deliberately not rebuilt per call: the checked-segment bits are part of the
state, and clearing them turns a one-off integrity check into a walk of a segment
on every operation. Nothing in the arena refers to a virtual address.

The generic `mulle_allocator` interface still hands out process-local `void *`
values, because that is the contract it implements. A pointer maps to its arena
offset by subtracting `base`, and back by adding it.

## Modes

Runtime behaviour is a set of bit flags (`enum mulle_mmap_allocator_bit`):

| Flag | Meaning |
| ---- | ------- |
| `default` | fixed capacity, private mapping, lock never taken |
| `locking` | serialise operations with the arena lock |
| `inflexible` | do not grow |
| `shared` | shared mapping semantics (inter-process) |
| `dont_free` | do not release the underlying resources on `_done` |
| `dont_scribble` | avoid scribbling freed memory (not implemented) |
| `growable` | expand into the reserved window on demand |

`default` is the empty set, and that includes `locking`: the arena lock is never
taken, which is 13-15% of the allocation hot path -- an atomic exchange and its
release per operation. That is only correct for an arena one process and one
thread touch, so `locking` is what says so. The polarity is worth stating plainly
because the arena metadata *is* the shared state now: a private arena used from
two threads without the bit is a data race on the free lists, not a racing
counter.

`shared` forces `locking`. It does not force `inflexible`: a shared region has
one capacity, but a growable one grows inside the window that every process has
already mapped, so it needs no owner to extend the mapping. `_attach` forces
`shared | dont_free`, and adds `growable` or `inflexible` depending on whether
the mapping is larger than the arena the header describes.

## Lifecycles

- **Create.** `_mulle_mmap_allocator_init(p, capacity, mode)` rounds the capacity
  up to whole pages, maps it (shared or anonymous), initializes the arena once and
  *adopts* it -- the region was just built here, so its first segment is taken as
  checked rather than walked back over. `_mulle_mmap_allocator_init_growable(p, initial_capacity,
  growth_limit, mode)` instead reserves `growth_limit` and starts the arena as one
  segment of `initial_capacity`. A `growth_limit` that is not larger than
  `initial_capacity` is just the fixed allocator.
- **Attach.** `_mulle_mmap_allocator_attach(p, handle, capacity, base_address)`
  takes an existing mapping's handle instead of creating one. See below.
- **Reset.** `_mulle_mmap_allocator_reset(p)` rebuilds the arena in place. It
  discards every existing allocation, so it requires exclusive access.
- **Done.** `_mulle_mmap_allocator_done(p)` releases the mapping according to the
  mode bits. There is no allocator state to destroy besides the mapping.

## Shared memory across processes

### What the child has to be given

- `fork()` on Unix clones the address space. The child inherits the mapping *and*
  the `struct mulle_mmap_allocator`, so both processes use the same arena at the
  same address. Nothing to do.
- `CreateProcess()` on Windows, and `fork()` + `exec()` on Unix, start a fresh
  address space. The child gets nothing, and `_init`/`_init_growable` always
  create *new* memory, so a child that used them would end up with a separate
  region. The child must instead take the handle and call `_attach`.

### The lock

The lock lives in the region too, as a `uint32_t` in the arena header, taken with
an atomic exchange and released with an atomic store:

```c
while( __atomic_exchange_n( &header->lock, 1, __ATOMIC_ACQUIRE) != 0)
   ;                                   /* GCC / Clang  */
InterlockedCompareExchange( &header->lock, 1, 0)   /* MSVC / Windows */
```

Atomics operate on physical addresses. Two processes sharing pages share the same
physical word, so an exchange in one is seen by the other and the same `int`
serializes them. No `PTHREAD_PROCESS_SHARED` mutex, no named OS primitive.

Cache coherency protocols (MESI/MOESI) work on physical addresses, not virtual
addresses or process ids. When one core does an atomic `xchg` on a cache line, the
other core's copy is invalidated and refetched — exactly as between threads of one
process. The hardware does not know about process boundaries.

### Offsets, not pointers

Free-list links and each block's distance back to its predecessor are offsets,
never addresses. The view turns an offset into a local pointer with
`local_base[ segment] + ( offset - offset_base[ segment])`.

Consequence: the region may be mapped at a **different address in every
process**, which is the whole point of the offset format — the old design, which
translated absolute pointers, could not do this.

Caveat, and it is the important one:

- A `void *` that *user code* stores inside a shared payload is an address in the
  process that wrote it. The allocator cannot relocate user data. A process that
  will dereference such a pointer must attach at the creator's address, so
  `_attach()` takes a `base_address` hint: supply the owner's address when
  payloads hold raw pointers, pass `NULL` to map anywhere when payloads store
  offsets.
- Growth does not move anything. The ceiling is reserved up front and mapped as
  one range, so segment addresses never change and raw pointers stay valid across
  growth.

### Handing the region to a child

**Creator**, with the `shared` mode bit:

- POSIX — `mulle_mmap_alloc_shared_memory()`: `shm_open(name, 0600)` + immediate
  `shm_unlink` (the object lives while an fd is open) + `mmap(..., MAP_SHARED, fd,
  0)`. `FD_CLOEXEC` is cleared so the fd survives `exec`, which is what makes
  attach-after-exec possible.
- Windows — `CreateFileMappingA()` with `bInheritHandle = TRUE` +
  `MapViewOfFile()`. The handle is passed to the child.

**Child:**

- `_attach( p, handle, capacity, base_address)` maps the region with
  `mulle_mmap_map_shared_memory(handle, size, preferred)`:
  - POSIX: `mmap( preferred, ..., MAP_SHARED, fd, 0)`
  - Windows: `MapViewOfFileEx( ..., preferred)`
  - `preferred` is a **hint**, and it is checked: a mapping that did not land on
    it is given back and the call reports failure, so a caller that needs a
    specific address is never silently relocated to another one.
- `_attach` reads `arena_capacity` out of the header, so the child does not need
  to know how the creator split the window from the first segment. It then checks
  the versioned header, the configuration word and the segment directory,
  rejecting a stale, zeroed or foreign region through `p->fail` instead of
  attaching to it. It does not read the arena's blocks: each segment is checked
  by the first allocation or free that lands in it, so attaching to a cold region
  does not pull it back in, and a segment that fails that check fails the
  operation that found it, through `p->fail`. It never reinitializes shared data.
- `_attach` forces the mode to `shared | dont_free`, plus `growable` when the
  mapping is larger than the arena the header describes and `inflexible` when it
  is not — so `_done()` on the child leaves the mapping alone and only the creator
  releases it.

### Failure modes

| Situation | Result |
| --------- | ------ |
| Hint address occupied (or misaligned) | `mulle_mmap_map_shared_memory()` fails, `_attach` calls `p->fail` |
| Region stale, zeroed or foreign | header / directory check fails in `_attach`, `p->fail` |
| A segment's blocks are corrupt | the first operation that lands in it fails, `p->fail` (checked build: a release build compiles the per-operation check out, see `mulle-dlmalloc/DESIGN.md`) |
| Child calls `_done()` | `dont_free`: mapping is not unmapped, the creator still owns it |
| Grower dies mid-operation | see *Locking and crash policy* |

## Growth

Growth is the reserved-window scheme, not a file. `_init_growable` maps the
ceiling once, at one address, and the arena starts as a smaller first segment.
When an allocation finds the first segment full, the allocator grows the arena —
twice what it has, or the rest of the window, whichever is smaller — and retries.
Past `growth_limit` a request fails the ordinary way, through `p->fail`.

Because the window is one mapping, the range the embedder hands back is the bytes
right after segment 0, so dlmalloc extends that segment in place instead of
adding a second one: **a window arena is one segment for life**, however far it
grows. That is not an accounting detail. A second segment would cost dlmalloc's
single-segment fast path on every later operation, and a block never crosses a
segment boundary, so a request the arena had room for could fail because the room
was split by one — measured at 57% on the hot path, and at 2x the space for a
request already covered. The rest of this section follows from it.

```
 capacity        the window that was mapped (the ceiling)
 arena_capacity  the size segment 0 was created with, which is what
                 _init_growable was given; the segment grows past it
```

`_mulle_mmap_allocator_get_segment_count` therefore stays at 1 for a growable
allocator, and `_get_current_capacity` — the arena's high-water offset — is what
moves. `_get_grow_count` counts the growths this process asked for either way.

The window is mapped, not committed: the pages appear when they are first
touched, so the reserve costs address space rather than memory. Nothing on disk
backs it, because nothing has to — every process that attaches already holds the
mapping's handle, so there is no one who needs to *discover* the region, which is
the only thing a file would buy.

What the reservation does cost is the address itself: the window has to be free at
the owner's address in every process that needs raw-pointer payloads, because a
regularly-attached process maps the whole window at that address before it runs.
A small window always is free; a very large one need not be, and then `attach`
refuses the child rather than mapping it somewhere else, which is a clean failure
rather than a wrong one.

dlmalloc underneath can map each segment anywhere, and this embedder deliberately
does not use that freedom: a payload holding raw pointers needs one address in
every process, which the reserved window provides and per-segment mappings would
not.

A grow needs no new mapping and no syscall, so the critical section stays a few
stores. The arena's extent is bounded by `growth_limit`, and there is no
directory entry for it to run out of: the window is one segment, so
`DLMALLOC_OFFSET_MAX_SEGMENTS` never comes into it.

Capacity queries — `_get_segment_count`, `_get_current_capacity`,
`_get_initial_capacity`, `_get_grow_count` — read the shared header, so every
process sees the growth of every other. `_get_grow_count` is the exception: it is
process-local and attributes who grew, rather than describing the arena.

`_get_locking` is the other exception, and it is not a capacity query at all. It
answers from the process's own `mode`, because whether the arena lock is taken is
a property of the embedder that bound the arena, not of the arena. It is the
only statement of the rule -- `shared` locks, a private arena locks when
`locking` was asked for -- and the arena is bound from that same answer, so the
flags a caller passes, what this reports and the lock the arena actually takes
cannot disagree. `test/10-first/locking.c` pins that, for every entry point that
can construct one, `attach` included: its mode is assembled separately from the
init paths, so it is where the flag was once lost.

## Locking and crash policy

One process-shared spin lock, no owner tracking, no crash recovery. A process that
dies while holding the lock wedges the arena, and an operation interrupted by a
crash can leave metadata inconsistent. The policy is to discard or reinitialize
(`_mulle_mmap_allocator_reset`) rather than recover. Because growth here is
arithmetic over an already-mapped window, a grow never widens the window during
which the lock is held with I/O in it.

## Limitations

- `dont_scribble` is accepted and ignored; no code path consults it.
- Raw-pointer payloads pin the mapping address for every process.
- A shared region is only as trustworthy as the processes that can map it: treat
  it as writable by any attaching process and only share it with trusted peers.
- There is no query for bytes used or free.

## Not implemented / history

- **Address negotiation** was sketched in an earlier revision: probe candidate
  base addresses from a short-lived child and commit to one that maps
  successfully. It is only needed when raw-pointer payloads force a specific
  address *and* the hint proves unreliable. It is not implemented; today attach
  simply fails and the caller retries, and offset-only payloads need no specific
  address at all.
- The earlier design targeted dlmalloc's `mspace` API:
  - `create_mspace_with_base()` and `struct malloc_state` at the base of the
    region — replaced by the offset arena's header and segment directory. The
    struct no longer stores an `mspace`, attach no longer compares `seg.base`, and
    `_reset` reinitializes the arena in place.
  - `USE_SPIN_LOCKS` / `INSECURE` and `MLOCK_T` — replaced by the arena's own
    header lock, so those defines are not part of this build.
  - Options A–D for the relocating problem — settled by Option B: arena-relative
    offsets. Only user payloads still need a fixed address, which is what the
    `base_address` hint is for.
  - `MAP_FIXED` — never used. The address is a verified hint; the measurements
    behind that decision are in `../mulle-mmap/research/`.
- A **file-backed** region is the variant for attaching by path or not reserving a
  ceiling up front. It is not built; see mulle-dlmalloc's `DESIGN.md`.

## Building and testing

```sh
mulle-sde test --platform linux run test/30-shared   # the multi-process suite
mulle-sde test --platform linux run                  # all of them
```

`--platform` must come before the subcommand; without it the platform list comes
from the environment and the run cross-builds for Windows. The `30-shared` tests
(`grow`, `fork`, `process`, `shared-overflow`, `dump-chunk-walk`) are the ones
worth running under valgrind. See [test/README.md](test/README.md).
