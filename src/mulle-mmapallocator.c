#include "include-private.h"

#include "mulle-mmapallocator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
 * The allocator behind this is mulle-dlmalloc's relocatable offset arena. It
 * keeps all allocator metadata in shared memory as arena-relative offsets, so
 * the region may be mapped at a different address in every process.
 *
 * The generic mulle_allocator interface still passes around process-local
 * void * values. A pointer is converted to its arena offset by subtracting the
 * local base and back again by adding it. Persistent references that user code
 * stores in shared payloads are still raw pointers, so callers must attach at
 * the creator's address (base_address) unless their payloads store offsets.
 *
 * DLMALLOC_EXPORT static hides the offset allocator symbols, exactly as it did
 * for dlmalloc before.
 */
#define DLMALLOC_EXPORT   static
#include "mulle-dlmalloc/src/dlmalloc.c"

// struct mulle_mmap_allocator stores the view inline, because a public header
// cannot name a type that comes from the embedded dlmalloc. This is what keeps
// the hand written size of that storage in step with the type it holds.
_Static_assert( sizeof( dlmalloc_offset_view) <=
                   sizeof( ((struct mulle_mmap_allocator *) 0)->view.bytes),
                "mulle_mmap_allocator.view is too small for "
                "dlmalloc_offset_view");


int   __MULLE_MMAPALLOCATOR_ranlib__;


uint32_t   mulle_mmapallocator_get_version( void)
{
   return( MULLE__MMAPALLOCATOR_VERSION);
}


/*
 * The reserved-window embedder.
 *
 * dlmalloc never maps anything itself: it asks the view for a segment. Here the
 * whole window was mapped at initialization, at the same address in every
 * process, so a new segment is not a mapping -- it is the next range of the
 * window, and the callback is arithmetic. Refusing when the window is used up
 * is what turns growth off at the limit the caller asked for.
 *
 * `backing` is the arena offset the segment will live at, so one process can
 * locate a segment another process added from the directory alone.
 */
static int   mmap_grow_segment( void *context,
                                size_t min_size,
                                uintptr_t *backing,
                                void **base,
                                uintptr_t *size)
{
   struct mulle_mmap_allocator   *p;
   struct dlmalloc_offset_header *header;
   uintptr_t                       avail;
   uintptr_t                       high;
   uintptr_t                       want;

   p      = (struct mulle_mmap_allocator *) context;
   header = (struct dlmalloc_offset_header *) p->base;

   high = header->total_offset_space;
   if( high >= (uintptr_t) p->capacity)
      return( 0);

   avail = (uintptr_t) p->capacity - high;
   want  = min_size ? (uintptr_t) min_size : DLMALLOC_OFFSET_MIN_SEGMENT;
   if( want > avail)
      want = avail;                    // the last segment fills the window
   want &= ~((uintptr_t) DLMALLOC_OFFSET_ALIGN - 1);
   if( want < DLMALLOC_OFFSET_MIN_BLOCK)
      return( 0);

   *backing = high;
   *base    = (char *) p->base + high;
   *size    = want;
   return( 1);
}


/* A segment another process added is already inside our mapping. */
static int   mmap_map_segment( void *context,
                               uintptr_t backing,
                               size_t size,
                               void **base)
{
   struct mulle_mmap_allocator   *p;

   p = (struct mulle_mmap_allocator *) context;
   if( backing + (uintptr_t) size > (uintptr_t) p->capacity)
      return( 0);
   *base = (char *) p->base + backing;
   return( 1);
}


static void   mmap_unmap_segment( void *context, void *base, size_t size)
{
   // one window, one mapping: the allocator releases it as a whole
   (void) context;
   (void) base;
   (void) size;
}


/* The view that lives in the allocator. Never rebuilds it, see bind_view. */
static dlmalloc_offset_view   *mulle_mmap_allocator_view( struct mulle_mmap_allocator *p)
{
   return( (dlmalloc_offset_view *) (void *) p->view.bytes);
}


/*
 * Bind the process-local view to the stored region. The view describes the
 * arena, not the mapping: its capacity is segment 0's size, which for a
 * growable allocator is smaller than the window.
 *
 * This used to happen on every call, by value. That quietly defeated the
 * per-segment cache inside the view: view->verified was zero again each time,
 * so segment_find re-walked a segment's whole block tiling on every operation.
 * Measured at 4x per doubling of the allocation count, and 540x slower than a
 * persistent view at 40000 allocations. It hit the fixed allocator too, since
 * its single segment is checked by the same path.
 *
 * The view is safe to share between threads of one process. Everything it
 * caches -- verified, epoch, segment_count, local_base, segment0 -- is only
 * ever written with the arena lock held, and that lock serializes the threads
 * of one process as well as the processes sharing the region. Only base,
 * capacity and the callbacks are written here, before the arena is handed out.
 */
static void   mulle_mmap_allocator_bind_view( struct mulle_mmap_allocator *p)
{
   dlmalloc_offset_view   *view;

   view = mulle_mmap_allocator_view( p);

   memset( view, 0, sizeof( *view));
   view->base     = p->base;
   view->capacity = p->arena_capacity ? p->arena_capacity : p->capacity;

   //
   //  Without locking the arena takes no lock at all: an atomic exchange plus
   //  its store per operation, measured at 17-29 ns aside a 120 ns hot path,
   //  13-15% of it. Which arenas may do that is decided in one place, the
   //  accessor, and this binds the arena from that same answer.
   //
   view->nolocking = ! mulle_mmap_allocator_get_locking( p);

   if( p->capacity > view->capacity)
   {
      view->grow_segment  = mmap_grow_segment;
      view->map_segment   = mmap_map_segment;
      view->unmap_segment = mmap_unmap_segment;
      view->context       = p;
   }
}


/* Arena offset of a payload pointer handed out by this allocator. */
static dlmalloc_offset_t   mulle_mmap_allocator_offset( struct mulle_mmap_allocator *p,
                                                        void *q)
{
   return( (dlmalloc_offset_t)((char *) q - (char *) p->base));
}


static void   *mmap_reallocarray( void *q, size_t n, size_t el_size, struct mulle_allocator *allocator)
{
   struct mulle_mmap_allocator   *m;
   dlmalloc_offset_view          *view;
   dlmalloc_offset_t             offset;
   size_t                        size;
   uintptr_t                      tries;

   if( MULLE_C_UNLIKELY( ! n || ! el_size))
      _mulle_allocator_abort( allocator, q, 0);

   m      = (struct mulle_mmap_allocator *) allocator;
   size   = mulle_allocator_size_multiply( allocator, n, el_size);
   offset = q ? mulle_mmap_allocator_offset( m, q) : 0;
   view   = mulle_mmap_allocator_view( m);

   //
   //  A growable arena turns a refusal into a bigger segment and a retry, so a
   //  request only fails once the window the caller asked for is used up. One
   //  segment is added per try; a fixed arena has no grow_segment and fails the
   //  way it always did.
   //
   for( tries = 0; tries < DLMALLOC_OFFSET_MAX_SEGMENTS; tries++)
   {
      offset = dlmalloc_offset_realloc( view, offset, size);
      if( offset)
         return( (char *) m->base + offset);
      if( ! view->grow_segment || ! dlmalloc_offset_arena_grow( view, size))
         break;
      m->grow_count++;
   }
   return( NULL);
}


static void   *mmap_calloc( size_t n, size_t size, struct mulle_allocator *allocator)
{
   struct mulle_mmap_allocator   *m;
   dlmalloc_offset_view          *view;
   dlmalloc_offset_t             offset;
   size_t                        bytes;
   uintptr_t                      tries;

   m     = (struct mulle_mmap_allocator *) allocator;
   bytes = mulle_allocator_size_multiply( allocator, n, size);
   view  = mulle_mmap_allocator_view( m);

   for( tries = 0; tries < DLMALLOC_OFFSET_MAX_SEGMENTS; tries++)
   {
      offset = dlmalloc_offset_calloc( view, n, size);
      if( offset)
         return( (char *) m->base + offset);
      if( ! view->grow_segment || ! dlmalloc_offset_arena_grow( view, bytes))
         break;
      m->grow_count++;
   }
   return( NULL);
}


static void   mmap_free( void *p, struct mulle_allocator *allocator)
{
   struct mulle_mmap_allocator   *m;
   dlmalloc_offset_view          *view;

   if( ! p)
      return;

   m    = (struct mulle_mmap_allocator *) allocator;
   view = mulle_mmap_allocator_view( m);
   dlmalloc_offset_free( view, mulle_mmap_allocator_offset( m, p));
}


static void   __mulle_mmap_allocator_init( struct mulle_mmap_allocator *p)
{
   p->calloc       = mmap_calloc;
   p->reallocarray = mmap_reallocarray;
   p->free         = mmap_free;
   p->fail         = mulle_allocation_fail,
   p->abafree      = mulle_allocator_no_aba_abort,
   p->aba          = 0;
}


static void   __mulle_mmap_allocator_set_region( struct mulle_mmap_allocator *p,
                                                 void *base,
                                                 size_t capacity,
                                                 size_t arena_capacity,
                                                 int mode)
{
   assert( p);
   assert( base);

   p->base           = base;
   p->capacity       = capacity;
   p->arena_capacity = arena_capacity;
   p->grow_count     = 0;
   p->mode           = mode;

   mulle_mmap_allocator_bind_view( p);
}


static size_t   __mulle_mmap_allocator_pages( size_t capacity)
{
   size_t   pagesize;
   size_t   n_pages;

   // zero capacity may lead to mmap failing, and we need enough room for
   // the arena header
   pagesize = mulle_mmap_get_system_pagesize();
   n_pages  = (capacity / pagesize) + ((capacity % pagesize) ? 1 : 0);
   if( n_pages == 0)
      n_pages = 1;
   return( n_pages * pagesize);
}


/*
 * The size segment 0 is created with in a growable arena. Only the window is a
 * mapping, so only it has to be a whole number of pages: the first segment is a
 * range inside that mapping. Rounding it to a page would report -- and let the
 * caller allocate -- more than was asked for, which on a 16 KiB-page system
 * turns an 8 KiB request into 16 KiB. So this keeps the requested size, aligned
 * the way the arena needs it, and only enlarges a request that cannot hold the
 * arena header, state, directory and one block at all.
 *
 * The floor mirrors dlmalloc_offset_arena_init(); if the two ever disagree, the
 * arena refuses to initialize and fails through the fail vector rather than
 * silently shrinking.
 */
static size_t   __mulle_mmap_allocator_segment( size_t capacity)
{
   size_t   blocks_offset;
   size_t   minimum;

   blocks_offset  = dlmalloc_offset_align_up(
                       sizeof( struct dlmalloc_offset_header));
   blocks_offset  = dlmalloc_offset_align_up( blocks_offset +
                       sizeof( struct dlmalloc_offset_state));
   blocks_offset  = dlmalloc_offset_align_up( blocks_offset +
                       DLMALLOC_OFFSET_MAX_SEGMENTS *
                       sizeof( struct dlmalloc_offset_segment));
   minimum        = blocks_offset + DLMALLOC_OFFSET_MIN_BLOCK;

   capacity = dlmalloc_offset_align_up( capacity);
   if( capacity < minimum)
      capacity = minimum;
   return( capacity);
}


/*
 * Map mapping_capacity bytes and start an arena whose first segment is
 * arena_capacity. The two are equal for the fixed allocator; a larger mapping
 * is the window a growable one expands into.
 */
static void   __mulle_mmap_allocator_make( struct mulle_mmap_allocator *p,
                                           size_t mapping_capacity,
                                           size_t arena_capacity,
                                           int mode)
{
   dlmalloc_offset_view          *view;
   mulle_mmap_file_t             shared_handle;
   void                          *base;

   base          = NULL;
   shared_handle = MULLE_MMAP_INVALID_HANDLE;

   if( mode & mulle_mmap_allocator_shared)
   {
      struct mulle_mmap_shared_memory   shared_mem;

      shared_mem = mulle_mmap_alloc_shared_memory( mapping_capacity);
      base = shared_mem.address;
      if( ! base)
         (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, mapping_capacity);
      shared_handle = shared_mem.handle;
      mode |= mulle_mmap_allocator_locking;
   }
   else
   {
      // the offset arena is always a fixed region, so it needs its own memory
      base = mulle_mmap_alloc_pages( mapping_capacity);
      if( ! base)
         (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, mapping_capacity);
   }

   __mulle_mmap_allocator_set_region( p, base, mapping_capacity, arena_capacity, mode);
   p->shared_handle = shared_handle;

   // set_region bound the view to the arena size, not the window: its capacity
   // is segment 0, and the arena is built only now
   //
   // adopt, not attach: this process just built the arena, so its first segment
   // is known good and walking it back would only re-read what was just written
   view = mulle_mmap_allocator_view( p);
   if( ! dlmalloc_offset_arena_init( base, arena_capacity) ||
       ! dlmalloc_offset_arena_adopt( view, base, arena_capacity))
      (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, mapping_capacity);
}


void   _mulle_mmap_allocator_init( struct mulle_mmap_allocator *p,
                                   size_t capacity,
                                   int mode)
{
   __mulle_mmap_allocator_init( p);
   capacity = __mulle_mmap_allocator_pages( capacity);
   __mulle_mmap_allocator_make( p, capacity, capacity,
                                mode | mulle_mmap_allocator_inflexible);
}


void   _mulle_mmap_allocator_init_growable( struct mulle_mmap_allocator *p,
                                           size_t initial_capacity,
                                           size_t growth_limit,
                                           int mode)
{
   __mulle_mmap_allocator_init( p);

   // the first segment is what the caller asked for, not a page: only the
   // window is a mapping and therefore a whole number of pages
   initial_capacity = __mulle_mmap_allocator_segment( initial_capacity);
   growth_limit     = __mulle_mmap_allocator_pages( growth_limit);
   if( growth_limit < initial_capacity)
      growth_limit = __mulle_mmap_allocator_pages( initial_capacity);

   __mulle_mmap_allocator_make( p, growth_limit, initial_capacity,
                                mode | ((growth_limit > initial_capacity)
                                           ? mulle_mmap_allocator_growable
                                           : mulle_mmap_allocator_inflexible));
}


int   _mulle_mmap_allocator_grow( struct mulle_mmap_allocator *p,
                                  size_t min_size)
{
   dlmalloc_offset_view   *view;

   if( ! p || ! p->base || p->arena_capacity >= p->capacity)
      return( 0);

   view = mulle_mmap_allocator_view( p);
   if( ! dlmalloc_offset_arena_grow( view, min_size))
      return( 0);
   p->grow_count++;
   return( 1);
}


size_t   _mulle_mmap_allocator_get_segment_count( struct mulle_mmap_allocator *p)
{
   if( ! p || ! p->base)
      return( 0);
   return( (size_t) ((struct dlmalloc_offset_header *) p->base)->segment_count);
}


size_t   _mulle_mmap_allocator_get_current_capacity( struct mulle_mmap_allocator *p)
{
   if( ! p || ! p->base)
      return( 0);
   return( (size_t) ((struct dlmalloc_offset_header *) p->base)->total_offset_space);
}


size_t   _mulle_mmap_allocator_get_grow_count( struct mulle_mmap_allocator *p)
{
   return( p ? p->grow_count : 0);
}


size_t   _mulle_mmap_allocator_get_initial_capacity( struct mulle_mmap_allocator *p)
{
   if( ! p)
      return( 0);
   return( p->arena_capacity ? p->arena_capacity : p->capacity);
}


/*
 * The one statement of when the arena takes its lock, so that bind_view cannot
 * disagree with what a caller asked for. shared implies the flag: dropping the
 * lock on an arena other processes share is a silent data race, and attach is a
 * path that sets shared without assembling a mode the way the init entry points
 * do.
 */
int   _mulle_mmap_allocator_get_locking( struct mulle_mmap_allocator *p)
{
   if( ! p)
      return( 0);
   return( (p->mode & (mulle_mmap_allocator_locking |
                       mulle_mmap_allocator_shared)) != 0);
}


void   _mulle_mmap_allocator_attach( struct mulle_mmap_allocator *p,
                                     mulle_mmap_file_t handle,
                                     size_t capacity,
                                     void *base_address)
{
   struct dlmalloc_offset_header   *header;
   dlmalloc_offset_view            *view;
   void                            *base;
   size_t                          arena_capacity;

   __mulle_mmap_allocator_init( p);

   base = mulle_mmap_map_shared_memory( handle, capacity, base_address);
   if( ! base)
      (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, capacity);

   //  The arena in the region may be smaller than the mapping: that gap is the
   //  window a growable arena expands into. It is read back out of the header
   //  instead of handed in, so a child only has to know the mapping and the
   //  address it lives at -- not how the creator split the two.
   arena_capacity = capacity;
   header = (struct dlmalloc_offset_header *) base;
   if( header->magic == DLMALLOC_OFFSET_MAGIC &&
       header->version == DLMALLOC_OFFSET_VERSION &&
       header->capacity >= sizeof( struct dlmalloc_offset_header) &&
       header->capacity <= (uintptr_t) capacity)
      arena_capacity = (size_t) header->capacity;

   __mulle_mmap_allocator_set_region( p,
                                      base,
                                      capacity,
                                      arena_capacity,
                                      mulle_mmap_allocator_shared
                                         | mulle_mmap_allocator_locking
                                         | mulle_mmap_allocator_dont_free
                                         | ((capacity > arena_capacity)
                                               ? mulle_mmap_allocator_growable
                                               : mulle_mmap_allocator_inflexible));
   p->shared_handle = handle;

   // Never reinitialize shared data on attach. The offset arena checks the
   // versioned header and the segment directory, which is what rejects a stale,
   // zeroed or foreign region; it does not compare a persisted pointer against
   // our mapping, and it does not walk the arena -- the blocks of a segment are
   // checked by the first allocation or free that lands in that segment, so
   // attaching does not read a region that may be cold. A segment that fails
   // that check fails the operation that found it, through p->fail.
   view = mulle_mmap_allocator_view( p);
   if( ! dlmalloc_offset_arena_attach( view, base, arena_capacity))
      (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, capacity);
}


void   _mulle_mmap_allocator_done( struct mulle_mmap_allocator *p){
   _mulle_allocator_invalidate( (struct mulle_allocator *) p);

   // There is nothing to destroy in the arena itself: it is plain shared
   // memory. Only release the mapping when we own it.
   if( p->base && ! (p->mode & mulle_mmap_allocator_dont_free))
   {
      if( p->mode & mulle_mmap_allocator_shared)
      {
         struct mulle_mmap_shared_memory   shared_mem;

         shared_mem.address = p->base;
         shared_mem.size    = p->capacity;
         shared_mem.handle  = p->shared_handle;
         mulle_mmap_free_shared_memory( &shared_mem);
      }
      else
         mulle_mmap_free_pages( p->base, p->capacity);
   }

   p->base           = NULL;
   p->capacity       = 0;
   p->arena_capacity = 0;
   p->grow_count     = 0;

   //  the view holds this process's pointers into a mapping that is gone now.
   //  Clearing base is what makes a call on a done allocator fail instead of
   //  dereferencing it.
   memset( p->view.bytes, 0, sizeof( p->view.bytes));
}


void   _mulle_mmap_allocator_reset( struct mulle_mmap_allocator *p)
{
   dlmalloc_offset_view   *view;

   // Rebuild the arena in place. This discards every existing allocation, so it
   // requires exclusive access to the region. A growable arena starts over at
   // its first segment; the mapping stays the window it always was.
   if( ! p->base)
      (*p->fail)( mulle_mmap_allocator_as_allocator( p), p->base, p->capacity);

   //  The arena starts over, so this process's view of it has to as well. A
   //  rebind clears the mapped segment bases as well as the checked bits; adopt
   //  alone would leave bases of segments that no longer exist, and refresh
   //  skips a segment whose base is already set.
   mulle_mmap_allocator_bind_view( p);

   // rebuilds it here, so adopt: nothing about the fresh arena needs checking
   view = mulle_mmap_allocator_view( p);
   if( ! dlmalloc_offset_arena_init( p->base, p->arena_capacity) ||
       ! dlmalloc_offset_arena_adopt( view, p->base, p->arena_capacity))
      (*p->fail)( mulle_mmap_allocator_as_allocator( p), p->base, p->capacity);
}


/*
 * Dump the arena header and the block tiling to stderr for debugging. Blocks
 * are printed in arena order; an in-use block reports the usable payload size,
 * a free block its full block size.
 */
void   _mulle_mmap_allocator_dump( struct mulle_mmap_allocator *p)
{
   struct dlmalloc_offset_header    *header;
   struct dlmalloc_offset_segment   *entry;
   struct dlmalloc_offset_segment   *directory;
   struct dlmalloc_offset_block     *block;
   dlmalloc_offset_view              *view;
   uintptr_t                          count;
   uintptr_t                          end;
   uintptr_t                          index;
   uintptr_t                          offset;
   uintptr_t                          size;

   if( ! p || ! p->base)
   {
      fprintf( stderr, "dump: allocator not initialized\n");
      return;
   }

   // enter() refreshes and locks, so a grown arena is dumped in full and no
   // other process is mid-way through changing it
   view = mulle_mmap_allocator_view( p);
   if( ! dlmalloc_offset_enter( view))
      return;

   header    = dlmalloc_offset_header_for( view);
   directory = dlmalloc_offset_directory_for( view);
   count     = header->segment_count;

   fprintf( stderr, "=== mulle_mmap_allocator dump ===\n");
   fprintf( stderr, "  base=%p  capacity=%zu  arena=%zu  segments=%llu\n",
            p->base, p->capacity, p->arena_capacity,
            (unsigned long long) count);
   fprintf( stderr, "  magic=%#llx  version=%u  configuration=%#llx\n",
            (unsigned long long) header->magic,
            (unsigned) header->version,
            (unsigned long long) header->configuration);
   fprintf( stderr, "  blocks offset=%llu  size=%llu\n",
            (unsigned long long) header->blocks_offset,
            (unsigned long long) header->blocks_size);
   //  the lock is already held here, so read the field rather than going
   //  through dlmalloc_offset_arena_corrupted()
   fprintf( stderr, "  corrupt=%s (this process)\n",
            view->corrupted ? "yes" : "no");

   for( index = 0; index < count; index++)
   {
      entry = &directory[ index];
      end   = entry->offset_base + entry->size;

      fprintf( stderr, "  segment %llu at offset %llu, %llu bytes:\n",
               (unsigned long long) index,
               (unsigned long long) entry->offset_base,
               (unsigned long long) entry->size);

      offset = entry->blocks_base;
      while( offset < end)
      {
         block = dlmalloc_offset_block_at( view, offset);
         if( ! block)
            break;
         size = dlmalloc_offset_block_size( block);
         if( ! size)
            break;
         if( dlmalloc_offset_block_inuse( block))
            fprintf( stderr, "    [%llu] INUSE  size=%llu\n",
                     (unsigned long long) offset,
                     (unsigned long long)(size - DLMALLOC_OFFSET_BLOCK_SIZE));
         else
            fprintf( stderr, "    [%llu] FREE   size=%llu\n",
                     (unsigned long long) offset,
                     (unsigned long long) size);
         offset += size;
      }
   }

   fprintf( stderr, "=================================\n");
   dlmalloc_offset_leave( view);
}


/*
 * extension : mulle-sde/c-demo
 * directory : demo/library
 * template  : .../PROJECT_NAME.PROJECT_EXTENSION
 * Suppress this comment with `export MULLE_SDE_GENERATE_FILE_COMMENTS=NO`
 */