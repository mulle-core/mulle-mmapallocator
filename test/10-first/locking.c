/*
  Which arenas take the arena lock.

  Without mulle_mmap_allocator_locking the arena lock is never taken at all,
  which is 13-15% of the allocation hot path, and that is only sound for an
  arena one process and one thread touch. A shared arena must lock.

  attach is the case worth pinning. It marks the arena shared without going
  through the mode assembly the two init entry points use, so it is the path
  where the lock can go missing, and it once did.
*/

#include <mulle-mmapallocator/mulle-mmapallocator.h>

#include <stdio.h>


static int   checks_failed;


static void   check( const char *what,
                     struct mulle_mmap_allocator *p,
                     int expect)
{
   int   got;

   got = mulle_mmap_allocator_get_locking( p);
   if( got != expect)
      ++checks_failed;
   printf( "%-30s locking=%d expected %d %s\n",
           what, got, expect, (got == expect) ? "ok" : "FAIL");
}


int   main( int argc, char *argv[])
{
   struct mulle_mmap_allocator   mmap_allocator;
   struct mulle_mmap_allocator   attached;

   mulle_mmap_allocator_init( &mmap_allocator, 0x4000,
                              mulle_mmap_allocator_default);
   check( "private, fixed", &mmap_allocator, 0);
   mulle_mmap_allocator_done( &mmap_allocator);

   mulle_mmap_allocator_init( &mmap_allocator, 0x4000,
                              mulle_mmap_allocator_locking);
   check( "private, locking asked for", &mmap_allocator, 1);
   mulle_mmap_allocator_done( &mmap_allocator);

   mulle_mmap_allocator_init_growable( &mmap_allocator, 0x4000, 0x40000,
                                       mulle_mmap_allocator_default);
   check( "private, growable", &mmap_allocator, 0);
   mulle_mmap_allocator_done( &mmap_allocator);

   mulle_mmap_allocator_init_growable( &mmap_allocator, 0x4000, 0x40000,
                                       mulle_mmap_allocator_locking);
   check( "private, growable, locking", &mmap_allocator, 1);
   mulle_mmap_allocator_done( &mmap_allocator);

   mulle_mmap_allocator_init( &mmap_allocator, 0x4000,
                              mulle_mmap_allocator_shared);
   check( "shared", &mmap_allocator, 1);

   mulle_mmap_allocator_attach( &attached,
                                mmap_allocator.shared_handle,
                                mmap_allocator.capacity,
                                NULL);
   check( "attached to it", &attached, 1);

   mulle_mmap_allocator_done( &attached);
   mulle_mmap_allocator_done( &mmap_allocator);

   return( checks_failed ? 1 : 0);
}
