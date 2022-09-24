#include <mulle-mmapallocator/mulle-mmapallocator.h>
#include <mulle-thread/mulle-thread.h>
#include <mulle-fifo/mulle-fifo.h>
#include <mulle-sprintf/mulle-sprintf.h>

#include <stdio.h>

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>


struct shared_memory
{
   struct mulle__pointerfifo32   child_parent;
   struct mulle__pointerfifo32   parent_child;
};


int   main( int argc, char *argv[])
{
   struct mulle_allocator        *allocator;
   struct mulle_mmap_allocator   mmap_allocator;
   char                          *s;
   char                          *s2;
   int                           wstatus;
   struct shared_memory          *shmem;
   int                           i;


   mulle_mmap_allocator_init( &mmap_allocator, 8192, mulle_mmap_allocator_shared);
   allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);


   shmem = mulle_allocator_calloc( allocator, 1, sizeof( struct shared_memory));

   _mulle__pointerfifo32_init( &shmem->child_parent);
   _mulle__pointerfifo32_init( &shmem->parent_child);

   i = 0;

   // now fork a process and access the date
   if( ! fork())
   {
      fprintf( stderr, "child is reading and writing shared memory\n");

      for(; i < 32;)
      {
         s = _mulle__pointerfifo32_read( &shmem->parent_child);
         if( ! s)
            continue; // busy wait

         fprintf( stderr, "child received %s\n", s);

         mulle_allocator_asprintf( allocator, &s2, "c->p %d", i++);

         fprintf( stderr, "child sends %s\n", s2);
         _mulle__pointerfifo32_write( &shmem->child_parent, s2);

         fprintf( stderr, "child frees %s\n", s);
         mulle_allocator_free( allocator, s);
      }

      fprintf( stderr, "child is exiting\n");
      exit( 0);
   }

   fprintf( stderr, "parent is reading and writing shared memory\n");

   s = NULL;
   for(; i < 32;)
   {
      mulle_allocator_asprintf( allocator, &s2, "p->c %d", i++);

      fprintf( stderr, "parent sends %s\n", s2);
      _mulle__pointerfifo32_write( &shmem->parent_child, s2);

      if( s)
      {
         fprintf( stderr, "parent frees %s\n", s);
         mulle_allocator_free( allocator, s);
      }
      s = _mulle__pointerfifo32_read( &shmem->child_parent);
      if( ! s)
         continue;
      fprintf( stderr, "parent received %s\n", s);
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
