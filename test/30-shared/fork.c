#include <mulle-mmapallocator/mulle-mmapallocator.h>

#include <stdio.h>

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>


struct shared_memory
{
   char   *s;
};


int   main( int argc, char *argv[])
{
   struct mulle_allocator        *allocator;
   struct mulle_mmap_allocator   mmap_allocator;
   char                          *s;
   int                           wstatus;
   struct shared_memory          *shmem;

   mulle_mmap_allocator_init( &mmap_allocator, 8192, mulle_mmap_allocator_shared);
   allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);


   shmem = mulle_allocator_calloc( allocator, 1, sizeof( struct shared_memory));

   // now fork a process and access the date
   if( ! fork())
   {
      shmem->s = mulle_allocator_strdup( allocator, "VfL Bochum 1848");

      //mulle_mmap_allocator_done( &mmap_allocator);
      fprintf( stderr, "child is exiting\n");
      exit( 0);
   }

   fprintf( stderr, "parent is waiting\n");
   wait( &wstatus);

   fprintf( stderr, "parent is reading shared memory\n");

   printf( "%s\n",  shmem->s);
   mulle_mmap_allocator_done( &mmap_allocator);

   return( 0);
}


/*
 * extension : mulle-sde/c-test-library-demo
 * directory : demo/all
 * template  : .../hello.c
 * Suppress this comment with `export MULLE_SDE_GENERATE_FILE_COMMENTS=NO`
 */
