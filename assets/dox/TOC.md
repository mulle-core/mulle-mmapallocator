# mulle-mmapallocator Library Documentation for AI

## 1. Introduction & Purpose

mulle-mmapallocator wraps the mulle-allocator interface around memory mapped I/O, enabling creation of memory allocations shared across process boundaries. It uses dlmalloc's mspace for efficient sub-heap allocation within an mmap'd region. Useful for inter-process communication, persistent memory structures, and isolated allocation spaces that can be reclaimed by destroying the allocator.

## 2. Key Concepts & Design Philosophy

- **Allocator Abstraction**: Implements mulle-allocator interface; drop-in replacement for other allocators
- **Mmap-Backed Storage**: Allocations come from memory-mapped pages, not heap
- **Sub-Heap (mspace)**: Uses dlmalloc's mspace for independent allocation management within mapped region
- **Process Sharing**: Can map shared memory files for inter-process allocation
- **Isolated Reclamation**: Entire allocation space reclaimed by simply destroying allocator
- **Pre-Allocation**: Shared memory size fixed at initialization time (cross-platform requirement)
- **Configuration Modes**: Bitflags for locking, flexibility, sharing, and cleanup behavior

## 3. Core API & Data Structures

### mulle-mmapallocator.h - Main API

#### struct mulle_mmap_allocator
- **Purpose**: Allocator that manages memory within an mmap'd region
- **Extends**: MULLE_ALLOCATOR_BASE (compatible with mulle-allocator interface)
- **Fields**:
  - `mspace`: dlmalloc mspace pointer for sub-heap allocation
  - `base`: Base address of mmap'd region
  - `capacity`: Total capacity of allocated region (~2KB less available to user)
  - `mode`: Bitflags controlling behavior (locking, sharing, etc.)

#### Configuration Modes (enum mulle_mmap_allocator_bit)

- `mulle_mmap_allocator_default` (0x0): Standard behavior
- `mulle_mmap_allocator_locking` (0x1): Add thread-safety with locks
- `mulle_mmap_allocator_inflexible` (0x2): Strict memory management; fail on issues
- `mulle_mmap_allocator_shared` (0x4): Create shared memory (for IPC)
- `mulle_mmap_allocator_dont_free` (0x8): Don't free mmap on destruction (for shared memory)
- `mulle_mmap_allocator_dont_scribble` (0x10): Skip security-oriented memory zeroing

#### Initialization & Lifecycle

- `_mulle_mmap_allocator_init(p, capacity, mode)` → `void`: Unchecked initialization; allocates mmap region of given capacity
- `mulle_mmap_allocator_init(p, capacity, mode)` → `void`: Safe version with NULL check
- `_mulle_mmap_allocator_reset(p)` → `void`: Unchecked reset; clears allocator state
- `mulle_mmap_allocator_reset(p)` → `void`: Safe version with NULL check
- `_mulle_mmap_allocator_done(p)` → `void`: Unchecked cleanup; destroys mspace and unmaps memory
- `mulle_mmap_allocator_done(p)` → `void`: Safe version with NULL check

#### Version Functions

- `mulle_mmapallocator_get_version()` → `uint32_t`: Returns version number
- `mulle_mmapallocator_get_version_major()` → `unsigned int`: Extracts major version
- `mulle_mmapallocator_get_version_minor()` → `unsigned int`: Extracts minor version
- `mulle_mmapallocator_get_version_patch()` → `unsigned int`: Extracts patch version

## 4. Performance Characteristics

- **Allocation**: O(1) amortized for typical allocations (dlmalloc performance within mspace)
- **Fragmentation**: Subject to dlmalloc fragmentation within the fixed-size region
- **Memory Overhead**: ~2KB overhead per allocator for metadata
- **Reclamation**: O(1) to free entire allocation space (single unmap)
- **Thread-Safety**: Only with `mulle_mmap_allocator_locking` flag; otherwise requires external locking
- **Capacity**: Fixed at initialization; cannot grow beyond initial allocation

## 5. AI Usage Recommendations & Patterns

### Best Practices

- **Fix Size Early**: Determine required capacity at initialization; resizing requires recreating allocator
- **Use for Isolation**: Great for temporary allocations or sandboxed computation
- **Shared Memory Setup**: Use `mulle_mmap_allocator_shared` flag for inter-process scenarios
- **Don't Free Flag**: Set `mulle_mmap_allocator_dont_free` for shared memory regions to persist
- **Lock Mode**: Use `mulle_mmap_allocator_locking` if allocator accessed from multiple threads

### Common Pitfalls

- **Capacity Overrun**: Will fail or error if capacity exhausted; no automatic growth
- **Sharing Complexity**: Shared memory requires careful synchronization between processes
- **Persistence**: Shared memory files persist on disk; clean up manually if not needed
- **Pointer Validity**: Pointers only valid in processes that map the same region
- **Mixed Allocators**: Don't mix pointers from different mmapallocators

### Idiomatic Usage

```c
// Pattern 1: Temporary isolated space
struct mulle_mmap_allocator alloc;
mulle_mmap_allocator_init(&alloc, 1024*1024, mulle_mmap_allocator_default);
void *p = mulle_allocator_malloc((struct mulle_allocator *)&alloc, 100);
mulle_mmap_allocator_done(&alloc);  // All memory freed at once

// Pattern 2: Shared memory between processes
struct mulle_mmap_allocator shared;
mulle_mmap_allocator_init(&shared, 10*1024*1024, 
    mulle_mmap_allocator_shared | mulle_mmap_allocator_dont_free);
// Both parent and child processes can use same allocator

// Pattern 3: Thread-safe allocations
struct mulle_mmap_allocator thread_safe;
mulle_mmap_allocator_init(&thread_safe, 2*1024*1024, 
    mulle_mmap_allocator_locking);
```

## 6. Integration Examples

### Example 1: Basic Mmap Allocator

```c
#include <mulle-mmapallocator/mulle-mmapallocator.h>
#include <mulle-allocator/mulle-allocator.h>
#include <stdio.h>

int main() {
    struct mulle_mmap_allocator alloc;
    
    mulle_mmap_allocator_init(&alloc, 1024*1024, 0);
    
    struct mulle_allocator *ma = (struct mulle_allocator *)&alloc;
    
    char *buf = mulle_allocator_malloc(ma, 256);
    snprintf(buf, 256, "Hello from mmap allocator");
    printf("%s\n", buf);
    
    mulle_allocator_free(ma, buf);
    mulle_mmap_allocator_done(&alloc);
    
    return 0;
}
```

### Example 2: Isolated Temporary Space

```c
#include <mulle-mmapallocator/mulle-mmapallocator.h>
#include <mulle-allocator/mulle-allocator.h>
#include <string.h>

void process_data_in_sandbox(const void *input, size_t input_len) {
    struct mulle_mmap_allocator sandbox;
    mulle_mmap_allocator_init(&sandbox, 512*1024, 0);
    
    struct mulle_allocator *alloc = (struct mulle_allocator *)&sandbox;
    
    void *temp = mulle_allocator_malloc(alloc, input_len);
    memcpy(temp, input, input_len);
    
    // Process temp data...
    
    // Everything freed at once
    mulle_mmap_allocator_done(&sandbox);
}

int main() {
    unsigned char data[1000] = { 0 };
    process_data_in_sandbox(data, sizeof(data));
    return 0;
}
```

### Example 3: Thread-Safe Allocator

```c
#include <mulle-mmapallocator/mulle-mmapallocator.h>
#include <mulle-allocator/mulle-allocator.h>
#include <pthread.h>

struct mulle_mmap_allocator g_alloc;

void *thread_func(void *arg) {
    struct mulle_allocator *alloc = (struct mulle_allocator *)&g_alloc;
    
    for (int i = 0; i < 100; i++) {
        void *p = mulle_allocator_malloc(alloc, 64);
        mulle_allocator_free(alloc, p);
    }
    
    return NULL;
}

int main() {
    mulle_mmap_allocator_init(&g_alloc, 4*1024*1024, 
                              mulle_mmap_allocator_locking);
    
    pthread_t threads[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, thread_func, NULL);
    }
    
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    
    mulle_mmap_allocator_done(&g_alloc);
    return 0;
}
```

### Example 4: Memory Block Arena

```c
#include <mulle-mmapallocator/mulle-mmapallocator.h>
#include <mulle-allocator/mulle-allocator.h>
#include <stdio.h>

typedef struct {
    void *ptr;
    size_t size;
} BlockInfo;

int main() {
    struct mulle_mmap_allocator arena;
    mulle_mmap_allocator_init(&arena, 2*1024*1024, 0);
    
    struct mulle_allocator *alloc = (struct mulle_allocator *)&arena;
    
    BlockInfo blocks[100];
    for (int i = 0; i < 100; i++) {
        blocks[i].size = (i + 1) * 1024;
        blocks[i].ptr = mulle_allocator_malloc(alloc, blocks[i].size);
        printf("Allocated block %d: %zu bytes\n", i, blocks[i].size);
    }
    
    // All blocks still valid within arena
    mulle_mmap_allocator_reset(&arena);  // or done() to clean up
    
    return 0;
}
```

### Example 5: Reset vs Done

```c
#include <mulle-mmapallocator/mulle-mmapallocator.h>
#include <mulle-allocator/mulle-allocator.h>

int main() {
    struct mulle_mmap_allocator alloc;
    mulle_mmap_allocator_init(&alloc, 1024*1024, 0);
    
    struct mulle_allocator *ma = (struct mulle_allocator *)&alloc;
    
    // First usage
    void *p1 = mulle_allocator_malloc(ma, 1000);
    
    // Reset clears state but keeps mmap
    mulle_mmap_allocator_reset(&alloc);
    
    // Can reuse allocator
    void *p2 = mulle_allocator_malloc(ma, 2000);
    
    // Done unmaps everything
    mulle_mmap_allocator_done(&alloc);
    
    return 0;
}
```

### Example 6: Checking Capacity

```c
#include <mulle-mmapallocator/mulle-mmapallocator.h>
#include <mulle-allocator/mulle-allocator.h>

int main() {
    struct mulle_mmap_allocator alloc;
    size_t requested = 10*1024*1024;
    
    mulle_mmap_allocator_init(&alloc, requested, 0);
    
    struct mulle_allocator *ma = (struct mulle_allocator *)&alloc;
    
    printf("Requested: %zu\n", requested);
    printf("Actual capacity: %zu\n", alloc.capacity);
    printf("Available: %zu (approx %zu less for metadata)\n", 
           alloc.capacity, requested - alloc.capacity);
    
    mulle_mmap_allocator_done(&alloc);
    return 0;
}
```

## 7. Dependencies

- mulle-c11
- mulle-allocator
- mulle-mmap
- mulle-dlmalloc (for mspace)
