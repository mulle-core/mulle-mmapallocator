#include <mulle-mmapallocator/mulle-mmapallocator.h>

#include <stdio.h>


// mulle-testallocator doesn't do anything here
// so you gotta valgrind it

int   main( int argc, char *argv[])
{
   struct mulle_mmap_allocator   mmap_allocator;
   struct mulle_allocator        *allocator;
   char                          *s;

   mulle_mmap_allocator_init( &mmap_allocator, 0, mulle_mmap_allocator_default);
   allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);
   {
      s = mulle_allocator_strdup( allocator, "VfL Bochum 1848");
      printf( "%s\n", s);
      mulle_allocator_free( allocator, s);
   }
   mulle_mmap_allocator_done( &mmap_allocator);

   return( 0);
}


/*
 * extension : mulle-sde/c-test-library-demo
 * directory : demo/all
 * template  : .../hello.c
 * Suppress this comment with `export MULLE_SDE_GENERATE_FILE_COMMENTS=NO`
 */
