#ifndef mulle_mmapallocator_h__
#define mulle_mmapallocator_h__

#include "include.h"

#include <mulle-mmap/mulle-mmap.h>
#include <stdint.h>

/*
 *  (c) 2022 Nat! Mulle kybernetiK
 *
 *  version:  major, minor, patch
 */
#define MULLE__MMAPALLOCATOR_VERSION  ((0UL << 20) | (3 << 8) | 0)


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
   //  NOT thread safe. This is the empty flag set, so it does not include
   //  mulle_mmap_allocator_locking: the arena lock is never taken at all, which
   //  is 13-15% of the allocation hot path and is only sound for an arena one
   //  process and one thread touch. A private arena reached from two threads
   //  without the locking bit is a data race on the free lists.
   mulle_mmap_allocator_default       = 0x0,
   //  Serialise arena operations with the arena lock. shared forces the flag,
   //  so an inter-process arena always locks.
   mulle_mmap_allocator_locking       = 0x1,
   mulle_mmap_allocator_inflexible    = 0x2,
   mulle_mmap_allocator_shared        = 0x4,
   mulle_mmap_allocator_dont_free     = 0x8,
   mulle_mmap_allocator_dont_scribble = 0x10,
   mulle_mmap_allocator_growable      = 0x20
};


struct mulle_mmap_allocator
{
   MULLE_ALLOCATOR_BASE;

//@private don't rely on these
   void                *base;
   size_t              capacity;         // the mapping
   size_t              arena_capacity;   // segment 0 of it; less when growable
   size_t              grow_count;       // segments THIS process added, not shared
   int                 mode;
   mulle_mmap_file_t   shared_handle;  // Handle for shared memory (Windows/Unix fd)

   //  Storage for the process-local arena view. It is opaque here because its
   //  type belongs to the embedded dlmalloc, which this header cannot include.
   //  It must survive across calls: the view caches which segments this process
   //  has already had checked, and a view rebuilt per call turns that cache into
   //  a walk of a segment on every operation. The implementation static-asserts
   //  that this is large enough.
   union
   {
      void         *pointer;
      long double   widest;
      char          bytes[ 768];
   }                   view;
};


// capacity available to user allocations will be ca 2KB less than what
// you order
void   _mulle_mmap_allocator_init( struct mulle_mmap_allocator *p,
                                   size_t capacity,
                                   int mode);
// Attach to shared memory created by another process. The implementation keeps
// all allocator metadata as arena-relative offsets, so the region can be mapped
// anywhere. base_address is passed through as the mapping hint: supply the
// creator's address to keep raw pointers stored in shared payloads valid, or
// NULL to map anywhere when only offsets are shared. Attach validates the
// versioned arena header and never reinitializes shared data.
void   _mulle_mmap_allocator_attach( struct mulle_mmap_allocator *p,
                                     mulle_mmap_file_t handle,
                                     size_t capacity,
                                     void *base_address);
void   _mulle_mmap_allocator_reset( struct mulle_mmap_allocator *p);
void   _mulle_mmap_allocator_done( struct mulle_mmap_allocator *p);
void   _mulle_mmap_allocator_dump( struct mulle_mmap_allocator *p);

// Like _mulle_mmap_allocator_init(), but the region is a reserve: the arena
// starts as one segment of initial_capacity and grows, on demand, into the
// growth_limit bytes that were mapped up front. Every process maps the same
// window, so a pointer stays valid in all of them. growth_limit is rounded up
// to the system page size; when it is not larger than initial_capacity this is
// the ordinary fixed allocator.
//
// No file backs the window: on shared memory the pages are committed when they
// are first touched, so the cost of the reserve is address space, not memory.
//
// Growing never fails while the window has room; past growth_limit a request
// fails the ordinary way, through the `fail` vector.
void   _mulle_mmap_allocator_init_growable( struct mulle_mmap_allocator *p,
                                           size_t initial_capacity,
                                           size_t growth_limit,
                                           int mode);

// Grow the arena by at least min_size, as long as the window has room. The
// window is one mapping, so this extends the arena's single segment and the
// segment count does not move; the current capacity does.
// Returns nonzero on success. Allocation grows on its own; this is for a
// caller that wants a segment added before it asks for anything.
int    _mulle_mmap_allocator_grow( struct mulle_mmap_allocator *p,
                                   size_t min_size);

// How many segments the arena has, and how many bytes of the window it is
// currently using. Both are read out of the shared header, so any process
// sees the growth of any other. For reporting, not for synchronizing on.
size_t _mulle_mmap_allocator_get_segment_count( struct mulle_mmap_allocator *p);
size_t _mulle_mmap_allocator_get_current_capacity( struct mulle_mmap_allocator *p);

// How many segments THIS process has added to the arena, and how many times it
// grew without adding one (a failed grow is not counted). The count is the
// process's own and is not stored in the shared region, so it says who did the
// growing rather than how big the arena is.
size_t _mulle_mmap_allocator_get_grow_count( struct mulle_mmap_allocator *p);

// The size of the arena's first segment. An arena that has grown holds
// allocations beyond this offset; a fixed one holds none, and an offset is the
// byte distance from the mapping's base.
size_t _mulle_mmap_allocator_get_initial_capacity( struct mulle_mmap_allocator *p);

// Whether the arena serialises its own operations with the arena lock. Without
// it the lock is never taken, which is 13-15% of the allocation hot path and is
// only sound for an arena one process and one thread touch.
//
// The rule lives here and nowhere else: shared always locks, and a private
// arena locks only when mulle_mmap_allocator_locking was asked for. The arena
// is bound from this answer, so the flag a caller passes, this and the lock the
// arena actually takes cannot disagree.
int    _mulle_mmap_allocator_get_locking( struct mulle_mmap_allocator *p);




static inline void   mulle_mmap_allocator_init( struct mulle_mmap_allocator *p,
                                                size_t capacity,
                                                int mode)
{
   if( ! p)
      return;

   _mulle_mmap_allocator_init( p, capacity, mode);
}


static inline void   mulle_mmap_allocator_attach( struct mulle_mmap_allocator *p,
                                                   mulle_mmap_file_t handle,
                                                   size_t capacity,
                                                   void *base_address)
{
   if( ! p)
      return;

   _mulle_mmap_allocator_attach( p, handle, capacity, base_address);
}


static inline void   mulle_mmap_allocator_init_growable( struct mulle_mmap_allocator *p,
                                                         size_t initial_capacity,
                                                         size_t growth_limit,
                                                         int mode)
{
   if( ! p)
      return;

   _mulle_mmap_allocator_init_growable( p, initial_capacity, growth_limit, mode);
}


static inline int   mulle_mmap_allocator_grow( struct mulle_mmap_allocator *p,
                                               size_t min_size)
{
   return( p ? _mulle_mmap_allocator_grow( p, min_size) : 0);
}


static inline size_t   mulle_mmap_allocator_get_segment_count( struct mulle_mmap_allocator *p)
{
   return( p ? _mulle_mmap_allocator_get_segment_count( p) : 0);
}


static inline size_t   mulle_mmap_allocator_get_current_capacity( struct mulle_mmap_allocator *p)
{
   return( p ? _mulle_mmap_allocator_get_current_capacity( p) : 0);
}


static inline size_t   mulle_mmap_allocator_get_grow_count( struct mulle_mmap_allocator *p)
{
   return( p ? _mulle_mmap_allocator_get_grow_count( p) : 0);
}


static inline size_t   mulle_mmap_allocator_get_initial_capacity( struct mulle_mmap_allocator *p)
{
   return( p ? _mulle_mmap_allocator_get_initial_capacity( p) : 0);
}


static inline int   mulle_mmap_allocator_get_locking( struct mulle_mmap_allocator *p)
{
   return( p ? _mulle_mmap_allocator_get_locking( p) : 0);
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
