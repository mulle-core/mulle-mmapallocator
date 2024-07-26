#include "include-private.h"

#include "mulle-mmapallocator.h"

#include <stdlib.h>


/*
 * This way we can keep dlmalloc effectively hidden
 */
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
                                                 mspace *m,
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
   mspace  *mspace;
   void    *base;
   size_t   pagesize;
   size_t   n_pages;

   base = NULL;

   __mulle_mmap_allocator_init( p);

   // zero capacity may lead to mmap failing, and we need enough room for
   // mspace
   pagesize = mulle_mmap_get_system_pagesize();
   n_pages  = (capacity / pagesize) + (capacity % pagesize) ? 1 : 0;
   if( n_pages == 0)
      n_pages = 1;
   capacity = n_pages * pagesize;

   if( mode & mulle_mmap_allocator_shared)
   {
      base  = mulle_mmap_alloc_shared_pages( capacity);
      if( ! base)
         (*p->fail)( mulle_mmap_allocator_as_allocator( p), base, capacity);
      mode |= mulle_mmap_allocator_inflexible;
   }
   else
   {
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


void   _mulle_mmap_allocator_done( struct mulle_mmap_allocator *p)
{
   _mulle_allocator_invalidate( (struct mulle_allocator *) p);

   destroy_mspace( p->mspace);
   p->mspace = NULL;

   if( p->base && ! (p->mode & mulle_mmap_allocator_dont_free))
      mulle_mmap_free_pages( p->base, p->capacity);
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
}

/*
 * extension : mulle-sde/c-demo
 * directory : demo/library
 * template  : .../PROJECT_NAME.PROJECT_EXTENSION
 * Suppress this comment with `export MULLE_SDE_GENERATE_FILE_COMMENTS=NO`
 */
