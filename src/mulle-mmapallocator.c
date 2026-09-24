#include "include-private.h"

#include "mulle-mmapallocator.h"

#include <stdio.h>
#include <stdlib.h>


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


int   __MULLE_MMAPALLOCATOR_ranlib__;


uint32_t   mulle_mmapallocator_get_version( void)
{
   return( MULLE__MMAPALLOCATOR_VERSION);
}


/* Build a process-local view from the stored region. */
static dlmalloc_offset_view   mulle_mmap_allocator_view( struct mulle_mmap_allocator *p)
{
   dlmalloc_offset_view   view;

   view.base     = p->base;
   view.capacity = p->capacity;
   return( view);
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
   dlmalloc_offset_view          view;
   dlmalloc_offset_t             offset;
   size_t                        size;

   if( MULLE_C_UNLIKELY( ! n || ! el_size))
      _mulle_allocator_abort( allocator, q, 0);

   m      = (struct mulle_mmap_allocator *) allocator;
   size   = mulle_allocator_size_multiply( allocator, n, el_size);
   view   = mulle_mmap_allocator_view( m);
   offset = q ? mulle_mmap_allocator_offset( m, q) : 0;
   offset = dlmalloc_offset_realloc( &view, offset, size);
   return( offset ? (char *) m->base + offset : NULL);
}


static void   *mmap_calloc( size_t n, size_t size, struct mulle_allocator *allocator)
{
   struct mulle_mmap_allocator   *m;
   dlmalloc_offset_view          view;
   dlmalloc_offset_t             offset;

   m      = (struct mulle_mmap_allocator *) allocator;
   view   = mulle_mmap_allocator_view( m);
   offset = dlmalloc_offset_calloc( &view, n, size);
   return( offset ? (char *) m->base + offset : NULL);
}


static void   mmap_free( void *p, struct mulle_allocator *allocator)
{
   struct mulle_mmap_allocator   *m;
   dlmalloc_offset_view          view;

   if( ! p)
      return;

   m    = (struct mulle_mmap_allocator *) allocator;
   view = mulle_mmap_allocator_view( m);
   dlmalloc_offset_free( &view, mulle_mmap_allocator_offset( m, p));
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
                                                 int mode)
{
   assert( p);
   assert( base);

   p->base     = base;
   p->capacity = capacity;
   p->mode     = mode;
}


void   _mulle_mmap_allocator_init( struct mulle_mmap_allocator *p,
                                   size_t capacity,
                                   int mode)
{
   dlmalloc_offset_view          view;
   mulle_mmap_file_t             shared_handle;
   void                          *base;
   size_t                        pagesize;
   size_t                        n_pages;

   base          = NULL;
   shared_handle = MULLE_MMAP_INVALID_HANDLE;

   __mulle_mmap_allocator_init( p);

   // zero capacity may lead to mmap failing, and we need enough room for
   // the arena header
   pagesize = mulle_mmap_get_system_pagesize();
   n_pages  = (capacity / pagesize) + ((capacity % pagesize) ? 1 : 0);
   if( n_pages == 0)
      n_pages = 1;
   capacity = n_pages * pagesize;

   if( mode & mulle_mmap_allocator_shared)
   {
      struct mulle_mmap_shared_memory   shared_mem;

      shared_mem = mulle_mmap_alloc_shared_memory( capacity);
      base = shared_mem.address;
      if( ! base)
         (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, capacity);
      shared_handle = shared_mem.handle;
      mode |= mulle_mmap_allocator_inflexible | mulle_mmap_allocator_locking;
   }
   else
   {
      // the offset arena is always a fixed region, so it needs its own memory
      base = mulle_mmap_alloc_pages( capacity);
      if( ! base)
         (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, capacity);
      mode |= mulle_mmap_allocator_inflexible;
   }

   if( ! dlmalloc_offset_arena_init( base, capacity) ||
       ! dlmalloc_offset_arena_attach( &view, base, capacity))
      (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, capacity);

   __mulle_mmap_allocator_set_region( p, base, capacity, mode);
   p->shared_handle = shared_handle;
}


void   _mulle_mmap_allocator_attach( struct mulle_mmap_allocator *p,
                                     mulle_mmap_file_t handle,
                                     size_t capacity,
                                     void *base_address)
{
   dlmalloc_offset_view   view;
   void                   *base;

   __mulle_mmap_allocator_init( p);

   base = mulle_mmap_map_shared_memory( handle, capacity, base_address);
   if( ! base)
      (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, capacity);

   // Never reinitialize shared data on attach. The offset arena validates the
   // versioned header and walks the region once to reject a stale or corrupt
   // one; it does not compare a persisted pointer against our mapping.
   if( ! dlmalloc_offset_arena_attach( &view, base, capacity))
      (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, capacity);

   __mulle_mmap_allocator_set_region( p,
                                      base,
                                      capacity,
                                      mulle_mmap_allocator_shared
                                         | mulle_mmap_allocator_inflexible
                                         | mulle_mmap_allocator_dont_free);
   p->shared_handle = handle;
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

   p->base     = NULL;
   p->capacity = 0;
}


void   _mulle_mmap_allocator_reset( struct mulle_mmap_allocator *p)
{
   dlmalloc_offset_view   view;

   // Rebuild the arena in place. This discards every existing allocation, so it
   // requires exclusive access to the region.
   if( ! p->base ||
       ! dlmalloc_offset_arena_init( p->base, p->capacity) ||
       ! dlmalloc_offset_arena_attach( &view, p->base, p->capacity))
      (*p->fail)( mulle_mmap_allocator_as_allocator( p), p->base, p->capacity);
}


/*
 * Dump the arena header and the block tiling to stderr for debugging. Blocks
 * are printed in arena order; an in-use block reports the usable payload size,
 * a free block its full block size.
 */
void   _mulle_mmap_allocator_dump( struct mulle_mmap_allocator *p)
{
   struct dlmalloc_offset_header   *header;
   struct dlmalloc_offset_block    *block;
   dlmalloc_offset_view            view;
   uint64_t                        end;
   uint64_t                        offset;
   uint64_t                        size;

   if( ! p || ! p->base)
   {
      fprintf( stderr, "dump: allocator not initialized\n");
      return;
   }

   view = mulle_mmap_allocator_view( p);
   if( ! dlmalloc_offset_lock( &view))
      return;

   header = dlmalloc_offset_header_for( &view);
   end    = header->blocks_offset + header->blocks_size;

   fprintf( stderr, "=== mulle_mmap_allocator dump ===\n");
   fprintf( stderr, "  base=%p  capacity=%zu\n", p->base, p->capacity);
   fprintf( stderr, "  magic=%#llx  version=%u  configuration=%#llx\n",
            (unsigned long long) header->magic,
            (unsigned) header->version,
            (unsigned long long) header->configuration);
   fprintf( stderr, "  blocks offset=%llu  size=%llu\n",
            (unsigned long long) header->blocks_offset,
            (unsigned long long) header->blocks_size);
   fprintf( stderr, "  blocks (walking the tiling):\n");

   offset = header->blocks_offset;
   while( offset < end)
   {
      block = dlmalloc_offset_block_at( &view, offset);
      size  = dlmalloc_offset_block_size( block);
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

   fprintf( stderr, "=================================\n");
   dlmalloc_offset_unlock( &view);
}


/*
 * extension : mulle-sde/c-demo
 * directory : demo/library
 * template  : .../PROJECT_NAME.PROJECT_EXTENSION
 * Suppress this comment with `export MULLE_SDE_GENERATE_FILE_COMMENTS=NO`
 */