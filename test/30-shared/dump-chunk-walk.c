#include <mulle-mmapallocator/mulle-mmapallocator.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# include <io.h>
#else
# include <unistd.h>
#endif


// The dump writes to stderr, so capture it into a temporary file, parse the
// first live block's reported size out of it and print a stable line to
// stdout. A 100 byte request rounds up to the 16 byte payload alignment and
// adds the 16 byte block header, giving a 128 byte block; the dump reports the
// usable payload, i.e. 128 - 16 = 112.
//
// The bug this pins: a dump that starts the walk at the wrong place reports a
// size read out of the caller's 0xAB filled payload instead of the block
// header.

static size_t   first_inuse_size( FILE *fp)
{
   char    line[ 512];
   size_t  size;
   char   *p;

   rewind( fp);
   while( fgets( line, sizeof( line), fp))
   {
      p = strstr( line, "INUSE");
      if( ! p)
         continue;
      p = strstr( p, "size=");
      if( ! p)
         continue;
      size = (size_t) strtoull( p + 5, NULL, 10);
      return( size);
   }
   return( (size_t) -1);
}


int   main( int argc, char *argv[])
{
   struct mulle_mmap_allocator   a;
   struct mulle_allocator        *allocator;
   void                          *live;
   FILE                          *capture;
   int                           saved_fd;
   size_t                        size;

   mulle_mmap_allocator_init( &a, 16 * 1024, mulle_mmap_allocator_shared);
   allocator = mulle_mmap_allocator_as_allocator( &a);

   live = mulle_allocator_malloc( allocator, 100);
   if( ! live)
   {
      fprintf( stdout, "allocation failed\n");
      mulle_mmap_allocator_done( &a);
      return( 0);
   }
   memset( live, 0xAB, 100);

   capture  = tmpfile();
   saved_fd = dup( 2);
   fflush( stderr);
   dup2( fileno( capture), 2);

   _mulle_mmap_allocator_dump( &a);

   fflush( stderr);
   dup2( saved_fd, 2);
   close( saved_fd);

   // let the allocator see the live (non-freed) pointer again
   size = first_inuse_size( capture);
   fclose( capture);

   fprintf( stdout, "first INUSE size=%zu\n", size);

   mulle_mmap_allocator_done( &a);
   return( 0);
}


/*
 * extension : mulle-sde/c-test-library-demo
 * directory : demo/all
 * template  : .../hello.c
 * Suppress this comment with `export MULLE_SDE_GENERATE_FILE_COMMENTS=NO`
 */