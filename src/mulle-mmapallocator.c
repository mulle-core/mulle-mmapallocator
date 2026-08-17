#include "include-private.h"

#include "mulle-mmapallocator.h"

#include <stdlib.h>


/*
 * This way we can keep dlmalloc effectively hidden.
 * USE_SPIN_LOCKS: enables atomic spin locks in malloc_state, which live in
 * shared memory and work across processes (atomics operate on physical addresses).
 * INSECURE: disables magic/security checks that use per-process random values,
 * which would fail when attaching to a shared mspace from another process.
 */
#define USE_SPIN_LOCKS    1
#define INSECURE          1
#define DLMALLOC_EXPORT   static
#include "mulle-dlmalloc/src/dlmalloc.c"


int   __MULLE_MMAPALLOCATOR_ranlib__;


uint32_t   mulle_mmapallocator_get_version( void)
{
   return( MULLE__MMAPALLOCATOR_VERSION);
}


static void   *mmap_realloc( void *q, size_t size, struct mulle_allocator *allocator)
{
   struct mulle_mmap_allocator *m = (struct mulle_mmap_allocator *) allocator;

   return( mspace_realloc( m->mspace, q, size));
}


static void   *mmap_calloc( size_t n, size_t size, struct mulle_allocator *allocator)
{
   struct mulle_mmap_allocator *m = (struct mulle_mmap_allocator *) allocator;

   return( mspace_calloc( m->mspace, n, size));
}


static void   mmap_free( void *p, struct mulle_allocator *allocator)
{
   struct mulle_mmap_allocator *m = (struct mulle_mmap_allocator *) allocator;

   mspace_free( m->mspace, p);
}


static void   __mulle_mmap_allocator_init( struct mulle_mmap_allocator *p)
{
   p->calloc   = mmap_calloc;
   p->realloc  = mmap_realloc;
   p->free     = mmap_free;
   p->fail     = mulle_allocation_fail,
   p->abafree  = mulle_allocator_no_aba_abort,
   p->aba      = 0;
}


static void   __mulle_mmap_allocator_set_mspace( struct mulle_mmap_allocator *p,
                                                 void *m,
                                                 void *base,
                                                 size_t capacity,
                                                 int mode)
{
   assert( p);
   assert( m);

   p->mspace   = m;
   p->base     = base;
   p->capacity = capacity;
   p->mode     = mode;
}


void   _mulle_mmap_allocator_init( struct mulle_mmap_allocator *p,
                                   size_t capacity,
                                   int mode)
{
   mspace   mspace;
   void    *base;
   size_t   pagesize;
   size_t   n_pages;

   base = NULL;

   __mulle_mmap_allocator_init( p);

   // zero capacity may lead to mmap failing, and we need enough room for
   // mspace
   pagesize = mulle_mmap_get_system_pagesize();
   n_pages  = (capacity / pagesize) + ((capacity % pagesize) ? 1 : 0);
   if( n_pages == 0)
      n_pages = 1;
   capacity = n_pages * pagesize;

   if( mode & mulle_mmap_allocator_shared)
   {
      struct mulle_mmap_shared_memory shared_mem;
      
      shared_mem = mulle_mmap_alloc_shared_memory( capacity);
      base = shared_mem.address;
      if( ! base)
         (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, capacity);
      p->shared_handle = shared_mem.handle;
      mode |= mulle_mmap_allocator_inflexible | mulle_mmap_allocator_locking;
   }
   else
   {
      p->shared_handle = MULLE_MMAP_INVALID_HANDLE;
      if( mode & mulle_mmap_allocator_inflexible)
      {
         base = mulle_mmap_alloc_pages( capacity);
         if( ! base)
            (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, capacity);
      }
   }

   if( ! base)
      mspace = create_mspace( capacity, (mode & mulle_mmap_allocator_locking));
   else
      mspace = create_mspace_with_base( base, capacity, mode & (mulle_mmap_allocator_inflexible|mulle_mmap_allocator_locking));

   if( ! mspace)
      (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, capacity);

   __mulle_mmap_allocator_set_mspace( p, mspace, base, capacity, mode);
}


void   _mulle_mmap_allocator_attach( struct mulle_mmap_allocator *p,
                                     mulle_mmap_file_t handle,
                                     size_t capacity,
                                     void *base_address)
{
   void    *base;
   mstate   ms;

   __mulle_mmap_allocator_init( p);
   ensure_initialization();

   base = mulle_mmap_map_shared_memory( handle, capacity, base_address);
   if( ! base)
      (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, capacity);

   // mspace is NOT at base - dlmalloc places a chunk header before malloc_state.
   // Replicate create_mspace_with_base's layout: mstate = chunk2mem(align_as_chunk(base))
   ms = (mstate) chunk2mem( align_as_chunk( base));

   // Validate that the mapped region contains a valid mspace created by the
   // parent. seg.base must match our mapping address (catches ASLR mismatch
   // or stale/zeroed memory) and seg.size must match expected capacity.
   if( ms->seg.base != (char *) base || ms->seg.size != capacity)
      (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, capacity);

   __mulle_mmap_allocator_set_mspace( p,
                                      ms,
                                      base,
                                      capacity,
                                      mulle_mmap_allocator_shared
                                         | mulle_mmap_allocator_inflexible
                                         | mulle_mmap_allocator_dont_free);
   p->shared_handle = handle;
}


void   _mulle_mmap_allocator_done( struct mulle_mmap_allocator *p){
   _mulle_allocator_invalidate( (struct mulle_allocator *) p);

   destroy_mspace( p->mspace);
   p->mspace = NULL;

   if( p->base && ! (p->mode & mulle_mmap_allocator_dont_free))
   {
      if( p->mode & mulle_mmap_allocator_shared)
      {
         struct mulle_mmap_shared_memory shared_mem;
         shared_mem.address = p->base;
         shared_mem.size = p->capacity;
         shared_mem.handle = p->shared_handle;
         mulle_mmap_free_shared_memory( &shared_mem);
      }
      else
         mulle_mmap_free_pages( p->base, p->capacity);
   }
}


void   _mulle_mmap_allocator_reset( struct mulle_mmap_allocator *p)
{
   destroy_mspace( p->mspace);

   // if we used create_mspace_with_base, destroy_mspace will not have deleted
   // out p->base so we can just reconfigure it
   if( ! p->base)
      p->mspace = create_mspace( p->capacity, (p->mode & mulle_mmap_allocator_locking));
   else
      p->mspace = create_mspace_with_base( p->base,
                                           p->capacity,
                                           p->mode & (mulle_mmap_allocator_inflexible|mulle_mmap_allocator_locking));

   if( ! p->mspace)
      (*p->fail)( mulle_mmap_allocator_as_allocator( p), p->base, p->capacity);
}

/*
 * Dump the full mspace state to stderr for debugging.
 * Walks all chunks in the shared region and hex-dumps the first 32 bytes
 * of each allocated chunk.
 */
void   _mulle_mmap_allocator_dump( struct mulle_mmap_allocator *p)
{
   mstate      ms;
   mchunkptr   q;
   mchunkptr   top;
   size_t      chunksize_val;
   size_t      i;
   size_t      n;
   unsigned char *bytes;

   if( ! p || ! p->mspace || ! p->base)
   {
      fprintf( stderr, "dump: allocator not initialized\n");
      return;
   }

   ms  = (mstate) p->mspace;
   top = ms->top;

   fprintf( stderr, "=== mulle_mmap_allocator dump ===\n");
   fprintf( stderr, "  base=%p  capacity=%zu  mspace=%p\n",
            p->base, p->capacity, p->mspace);
   fprintf( stderr, "  mstate fields:\n");
   fprintf( stderr, "    smallmap=0x%x  treemap=0x%x\n", ms->smallmap, ms->treemap);
   fprintf( stderr, "    dvsize=%zu  topsize=%zu\n",     ms->dvsize,   ms->topsize);
   fprintf( stderr, "    dv=%p  top=%p\n",               (void *) ms->dv, (void *) ms->top);
   fprintf( stderr, "    least_addr=%p\n",               (void *) ms->least_addr);
   fprintf( stderr, "    footprint=%zu  max_footprint=%zu  footprint_limit=%zu\n",
            ms->footprint, ms->max_footprint, ms->footprint_limit);
   fprintf( stderr, "    mflags=0x%x  magic=0x%zx\n",   ms->mflags, ms->magic);
#if USE_LOCKS
   fprintf( stderr, "    mutex=%d\n",                    ms->mutex);
#endif
   fprintf( stderr, "    seg.base=%p  seg.size=%zu  seg.sflags=0x%x\n",
            (void *) ms->seg.base, ms->seg.size, ms->seg.sflags);

   fprintf( stderr, "  chunks (walking from mstate end):\n");

   // first chunk starts right after the malloc_state header
   q = (mchunkptr)((char *) ms + pad_request( sizeof( struct malloc_state)));

   while( q && q < top)
   {
      chunksize_val = chunksize( q);
      if( chunksize_val == 0)
      {
         fprintf( stderr, "    [%p] chunksize=0, stopping walk\n", (void *) q);
         break;
      }

      if( cinuse( q))
      {
         bytes = (unsigned char *) chunk2mem( q);
         n     = chunksize_val - overhead_for( q);
         if( n > 32) n = 32;
         fprintf( stderr, "    [%p] INUSE  size=%zu  data=",
                  (void *) q, chunksize_val - overhead_for( q));
         for( i = 0; i < n; i++)
            fprintf( stderr, "%02x ", bytes[i]);
         fprintf( stderr, "\n");
      }
      else
      {
         fprintf( stderr, "    [%p] FREE   size=%zu\n",
                  (void *) q, chunksize_val);
      }

      q = (mchunkptr)((char *) q + chunksize_val);
   }

   if( top)
      fprintf( stderr, "    [%p] TOP    size=%zu\n", (void *) top, ms->topsize);

   fprintf( stderr, "=================================\n");
}


/*
 * extension : mulle-sde/c-demo
 * directory : demo/library
 * template  : .../PROJECT_NAME.PROJECT_EXTENSION
 * Suppress this comment with `export MULLE_SDE_GENERATE_FILE_COMMENTS=NO`
 */
