#ifndef mulle_mmapallocator_h__
#define mulle_mmapallocator_h__

#include "include.h"

#include <stdint.h>

/*
 *  (c) 2022 Nat! Mulle kybernetiK
 *
 *  version:  major, minor, patch
 */
#define MULLE__MMAPALLOCATOR_VERSION  ((0UL << 20) | (1 << 8) | 0)


static inline unsigned int   mulle_mmapallocator_get_version_major( void)
{
   return( MULLE__MMAPALLOCATOR_VERSION >> 20);
}


static inline unsigned int   mulle_mmapallocator_get_version_minor( void)
{
   return( (MULLE__MMAPALLOCATOR_VERSION >> 8) & 0xFFF);
}


static inline unsigned int   mulle_mmapallocator_get_version_patch( void)
{
   return( MULLE__MMAPALLOCATOR_VERSION & 0xFF);
}

// mulle-c11 feature: MULLE_MMAPALLOCATOR__GLOBAL
uint32_t   mulle_mmapallocator_get_version( void);



enum mulle_mmap_allocator_bit
{
   mulle_mmap_allocator_default    = 0x0,
   mulle_mmap_allocator_locking    = 0x1,
   mulle_mmap_allocator_inflexible = 0x2,
   mulle_mmap_allocator_shared     = 0x4,
   mulle_mmap_allocator_dont_free  = 0x8
};


struct mulle_mmap_allocator
{
   MULLE_ALLOCATOR_BASE;

//@private don't rely on these
   void     *mspace;
   void     *base;
   size_t   capacity;
   int      mode;
};


// capacity available to user allocations will be ca 2KB less than what
// you order
void   _mulle_mmap_allocator_init( struct mulle_mmap_allocator *p,
                                   size_t capacity,
                                   int mode);
void   _mulle_mmap_allocator_reset( struct mulle_mmap_allocator *p);
void   _mulle_mmap_allocator_done( struct mulle_mmap_allocator *p);




static inline void   mulle_mmap_allocator_init( struct mulle_mmap_allocator *p,
                                                size_t capacity,
                                                int mode)
{
   if( ! p)
      return;

   _mulle_mmap_allocator_init( p, capacity, mode);
}


static inline void   mulle_mmap_allocator_reset( struct mulle_mmap_allocator *p)
{
   if( ! p)
      return;

   _mulle_mmap_allocator_reset( p);
}


static inline void   mulle_mmap_allocator_done( struct mulle_mmap_allocator *p)
{
   if( ! p)
      return;

   _mulle_mmap_allocator_done( p);
}


static inline struct mulle_allocator *
   mulle_mmap_allocator_as_allocator( struct mulle_mmap_allocator *p)
{
   return( (struct mulle_allocator *) p);
}


static inline struct mulle_mmap_allocator *
   mulle_allocator_as_mmap_allocator( struct mulle_allocator *p)
{
   return( (struct mulle_mmap_allocator *) p);
}

#endif


/*
 * extension : mulle-sde/c-demo
 * directory : demo/library
 * template  : .../PROJECT_NAME.h
 * Suppress this comment with `export MULLE_SDE_GENERATE_FILE_COMMENTS=NO`
 */
