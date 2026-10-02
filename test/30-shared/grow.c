#include <mulle-mmapallocator/mulle-mmapallocator.h>

#include <stdio.h>
#include <string.h>

#include <setjmp.h>

#ifndef _WIN32
# include <unistd.h>
# include <sys/types.h>
# include <sys/wait.h>
#endif


//
//  The reserved window.
//
//  A growable allocator maps its ceiling up front and starts the arena as a
//  smaller first segment, expanding into the reserve as allocations run out.
//  Unlike dlmalloc's own segment growth -- where each segment is mapped
//  wherever the system pleases -- the whole window is one mapping, so what the
//  arena grows into is the bytes after the segment it already has. dlmalloc
//  extends that segment in place rather than publishing a second one, which is
//  why the segment count below never moves: a window arena is one segment for
//  life, however far it grows. A second view of the region still resolves every
//  offset to an address of its own and finds the grown part of it. That is what
//  this test pins, in one process and then, with fork, in a second one.
//
//  mulle-testallocator doesn't do anything here, so you gotta valgrind it.
//

#define GROW_INITIAL      (8 * 1024)
#define GROW_LIMIT        (64 * 1024)
#define GROW_BLOCK        1024
#define GROW_MAX_BLOCKS   256
#define GROW_MARK         "the window is one mapping"


struct test_allocator
{
   struct mulle_mmap_allocator   mmap_allocator;
   jmp_buf                       failure;
};


// written by the child, read by the parent; lives in the shared region
struct verdict
{
   unsigned long long   segments;
   unsigned long long   grow;
   int                  ok;
};


static unsigned int   failures;


static int   report( const char *what, int ok)
{
   if( ok)
      printf( "ok   %s\n", what);
   else
   {
      printf( "FAIL %s\n", what);
      ++failures;
   }
   return( ok);
}


MULLE_C_NO_RETURN
static void   failure( struct mulle_allocator *p, void *block, size_t len)
{
   struct test_allocator   *test;

   // the allocator is the test struct's first member
   test = (struct test_allocator *) p;
   longjmp( test->failure, 1);
}


static unsigned char   pattern( unsigned int i)
{
   return( (unsigned char) ('a' + (i % 26)));
}


int   main( void)
{
   struct test_allocator         test;
   struct mulle_mmap_allocator   other;
   struct mulle_allocator        *allocator;
   char                          *blocks[ GROW_MAX_BLOCKS];
   unsigned int                  n;
   unsigned int                  i;
   size_t                        offset;
   int                           ok;

   mulle_mmap_allocator_init_growable( &test.mmap_allocator,
                                       GROW_INITIAL, GROW_LIMIT,
                                       mulle_mmap_allocator_shared);
   allocator       = mulle_mmap_allocator_as_allocator( &test.mmap_allocator);
   allocator->fail = failure;

   report( "the first segment is the size that was asked for",
           mulle_mmap_allocator_get_initial_capacity( &test.mmap_allocator) == GROW_INITIAL);
   report( "the window is larger than the first segment",
           test.mmap_allocator.capacity == GROW_LIMIT &&
           mulle_mmap_allocator_get_current_capacity( &test.mmap_allocator) == GROW_INITIAL);
   report( "the arena starts as one segment, grown by nobody",
           mulle_mmap_allocator_get_segment_count( &test.mmap_allocator) == 1 &&
           mulle_mmap_allocator_get_grow_count( &test.mmap_allocator) == 0);

   //
   //  Fill the first segment. The last request does not fit, so the allocator
   //  adds a segment itself and retries -- no setjmp involved, because a
   //  growable arena only refuses once its window is used up.
   //
   //
   //  Stop as soon as an allocation lands past the length the first segment was
   //  created with: the arena has grown into the reserve and what is being
   //  written is the part that did not exist before. Half the window is the
   //  backstop -- a growable arena refuses a request by calling the fail vector
   //  and there is no setjmp around this loop, so it must not run the window
   //  out, and an arena that grew cannot take half of it without getting past
   //  the first segment.
   //
   for( n = 0; n < GROW_LIMIT / GROW_BLOCK / 2; n++)
   {
      blocks[ n] = mulle_allocator_malloc( allocator, GROW_BLOCK);
      if( ! blocks[ n])
         break;
      memset( blocks[ n], pattern( n), GROW_BLOCK);
      offset = (size_t) (blocks[ n] - (char *) test.mmap_allocator.base);
      if( offset >= GROW_INITIAL)
         break;
   }

   //  It grows into the reserve rather than into a second segment, so the
   //  allocator reports a bigger arena and the same one segment.
   report( "filling the first segment grows the arena",
           n > 0 &&
           mulle_mmap_allocator_get_current_capacity( &test.mmap_allocator) > GROW_INITIAL);
   report( "the process that grew it says so",
           mulle_mmap_allocator_get_grow_count( &test.mmap_allocator) >= 1);
   report( "the window is still one segment",
           mulle_mmap_allocator_get_segment_count( &test.mmap_allocator) == 1);
   report( "an allocation lands past the first segment",
           offset >= GROW_INITIAL);

   ok = 1;
   for( i = 0; i <= n && i < GROW_MAX_BLOCKS; i++)
      if( ! blocks[ i] ||
          (unsigned char) blocks[ i][ 0] != pattern( i) ||
          (unsigned char) blocks[ i][ GROW_BLOCK - 1] != pattern( i))
         ok = 0;
   report( "everything written before the growth is intact", ok);

   //
   //  A second view of the same region, mapped wherever the system likes.
   //  The arena keeps offsets, so this one resolves an allocation in the grown
   //  segment without having mapped it at the creator's address.
   //
   mulle_mmap_allocator_attach( &other,
                                test.mmap_allocator.shared_handle,
                                test.mmap_allocator.capacity,
                                NULL);
   report( "a second attach lands somewhere else",
           other.base && other.base != test.mmap_allocator.base);
   report( "the second view sees every segment",
           mulle_mmap_allocator_get_segment_count( &other) ==
           mulle_mmap_allocator_get_segment_count( &test.mmap_allocator));
   report( "the second view grew nothing itself",
           mulle_mmap_allocator_get_grow_count( &other) == 0);
   report( "the second view resolves an allocation from the grown segment",
           offset &&
           memcmp( (char *) other.base + offset, blocks[ n], GROW_BLOCK) == 0);

   //
   //  Free and allocate again: the freed block must be reusable without
   //  disturbing what is in the grown segment.
   //
   mulle_allocator_free( allocator, blocks[ 0]);
   blocks[ 0] = mulle_allocator_malloc( allocator, GROW_BLOCK);
   report( "freeing and reallocating across the segments still works",
           blocks[ 0] &&
           memcmp( (char *) other.base + offset, blocks[ n], GROW_BLOCK) == 0);

   mulle_mmap_allocator_done( &other);

#ifndef _WIN32
   //
   //  A forked child, after the parent has grown. The child maps the region
   //  itself -- it deliberately does not use the mapping it inherited -- so it
   //  can only reach the grown segment by offset. That is the whole reason the
   //  window is a single mapping: a second process may attach anywhere and
   //  still find what the first one grew.
   //
   {
      struct verdict   *verdict;
      pid_t            pid;
      int              wstatus;

      verdict = mulle_allocator_calloc( allocator, 1, sizeof( struct verdict));
      memcpy( blocks[ n], GROW_MARK, sizeof( GROW_MARK));

      fflush( NULL);
      pid = fork();
      if( pid == 0)
      {
         struct mulle_mmap_allocator   child;

         mulle_mmap_allocator_attach( &child,
                                      test.mmap_allocator.shared_handle,
                                      test.mmap_allocator.capacity,
                                      NULL);
         verdict->grow     = (unsigned long long)
                                mulle_mmap_allocator_get_grow_count( &child);
         verdict->segments = (unsigned long long)
                                mulle_mmap_allocator_get_segment_count( &child);
         verdict->ok       = memcmp( (char *) child.base + offset,
                                     GROW_MARK, sizeof( GROW_MARK)) == 0;
         _exit( verdict->ok ? 0 : 1);
      }

      if( pid < 0 || waitpid( pid, &wstatus, 0) != pid)
         report( "a forked child attaches after the growth", 0);
      else
      {
         report( "a forked child attaches after the growth",
                 WIFEXITED( wstatus) && WEXITSTATUS( wstatus) == 0);
         report( "the child sees every segment",
                 verdict->segments == (unsigned long long)
                    mulle_mmap_allocator_get_segment_count( &test.mmap_allocator));
         report( "the child grew nothing itself",
                 verdict->grow == 0);
         report( "the child resolves the grown segment at its own address",
                 verdict->ok);
      }
   }
#endif

   //
   //  The window is the ceiling. Once it is used up a request fails the
   //  ordinary way, through the fail vector -- and this is the one place a jump
   //  is expected.
   //
   if( ! setjmp( test.failure))
   {
      for( i = 0; i < 1024; i++)
         mulle_allocator_malloc( allocator, GROW_BLOCK);
      report( "the window is a hard ceiling", 0);
   }
   else
      report( "the window is a hard ceiling", 1);

   mulle_mmap_allocator_done( &test.mmap_allocator);

   return( failures ? 1 : 0);
}
