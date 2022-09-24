#include <mulle-mmapallocator/mulle-mmapallocator.h>

#include <stdio.h>

#include <setjmp.h>


// mulle-testallocator doesn't do anything here
// so you gotta valgrind it

struct test_allocator
{
   struct mulle_mmap_allocator   mmap_allocator;
   jmp_buf                       failure;
};


MULLE_C_NO_RETURN
static void  failure( struct mulle_allocator *p, void *block, size_t len)
{
   struct test_allocator  *test = (struct test_allocator *) p;

   printf( "failed allocation (as expected)\n");
   longjmp( test->failure, 1);
}


int   main( int argc, char *argv[])
{
   struct mulle_allocator        *allocator;
   struct test_allocator         test;
   char                          *s;
   int                           i;

   mulle_mmap_allocator_init( &test.mmap_allocator, 8192, mulle_mmap_allocator_shared);
   allocator       = mulle_mmap_allocator_as_allocator( &test.mmap_allocator);
   allocator->fail = failure;

   if( ! setjmp( test.failure))
   {
      for( i = 0; i < 64; i++)
      {
         s = mulle_allocator_malloc( allocator, 1024);
         if( ! s)
         {
            fprintf( stderr, "mulle_allocator_malloc returned NULL\n");
            return( 1);
         }
         fprintf( stderr, "%d KB allocated\n", i + 1);
      }
      fprintf( stderr, "Unexpected allocation success\n");
      return( 1);
   }

   mulle_mmap_allocator_done( &test.mmap_allocator);

   return( 0);
}


/*
 * extension : mulle-sde/c-test-library-demo
 * directory : demo/all
 * template  : .../hello.c
 * Suppress this comment with `export MULLE_SDE_GENERATE_FILE_COMMENTS=NO`
 */
