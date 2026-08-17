#ifndef _WIN32

#include "include.h"

#include <mulle-mmapallocator/mulle-mmapallocator.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>


struct shared_memory
{
   char   *s;
};


int   main( void)
{
   struct mulle_allocator        *allocator;
   struct mulle_mmap_allocator   mmap_allocator;
   struct shared_memory          *shmem;
   int                           wstatus;

   mulle_mmap_allocator_init( &mmap_allocator, 8192, mulle_mmap_allocator_shared);
   allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);

   shmem = mulle_allocator_calloc( allocator, 1, sizeof( struct shared_memory));

   if( ! fork())
   {
      shmem->s = mulle_allocator_strdup( allocator, "VfL Bochum 1848");
      fprintf( stderr, "child is exiting\n");
      _exit( 0);
   }

   fprintf( stderr, "parent is waiting\n");
   wait( &wstatus);

   if( ! WIFEXITED( wstatus) || WEXITSTATUS( wstatus) != 0)
   {
      fprintf( stderr, "child failed (status %d)\n", wstatus);
      mulle_mmap_allocator_done( &mmap_allocator);
      return( 1);
   }

   fprintf( stderr, "parent is reading shared memory\n");

   printf( "%s\n", shmem->s);
   mulle_mmap_allocator_done( &mmap_allocator);

   return( 0);
}

#else

int   main( void)
{
   return( 0);
}

#endif /* _WIN32 */
