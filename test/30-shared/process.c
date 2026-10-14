#include "include.h"

#include <mulle-mmapallocator/mulle-mmapallocator.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# include <windows.h>
#else
# include <unistd.h>
# include <sys/types.h>
# include <sys/wait.h>
#endif


//
//  A second process that attaches to an existing shared region.
//
//  The region is mapped anew, at an address of the system's choosing -- not the
//  creator's -- so the two processes need not share a base. What they share is
//  the arena's offset space: the child allocates, stores the allocation's offset
//  in the region, and the parent resolves that offset against its own base. A raw
//  pointer stored in the region would only be valid in the process that wrote it,
//  which is exactly why this test keeps an offset instead.
//
//  The child is a real exec: it does not inherit the creator's mapping, it asks
//  for the region by handle. Attaching anywhere and resolving by offset is the
//  reason the arena keeps offsets rather than pointers, and this is the one place
//  that exercises it across a process boundary.
//

struct shared_memory
{
   size_t   offset;   // arena offset of the child's allocation
};


static int   run_child( int argc, char *argv[])
{
   struct mulle_mmap_allocator   mmap_allocator;
   struct mulle_allocator        *allocator;
   struct shared_memory          *shmem;
   char                          *s;
   mulle_mmap_file_t             handle;
   size_t                        capacity;
   size_t                        offset;

#ifdef _WIN32
   handle = (mulle_mmap_file_t)(uintptr_t) strtoull( argv[2], NULL, 16);
#else
   handle = (mulle_mmap_file_t)(intptr_t) strtoll( argv[2], NULL, 16);
#endif
   capacity = (size_t) strtoull( argv[3], NULL, 10);
   offset   = (size_t) strtoull( argv[4], NULL, 16);

   //  NULL: attach wherever the system maps it. The arena keeps offsets, so
   //  nothing here depends on this process landing at the creator's address.
   mulle_mmap_allocator_attach( &mmap_allocator, handle, capacity, NULL);
   allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);

   shmem = (struct shared_memory *) ((char *) mmap_allocator.base + offset);
   fprintf( stderr, "child: base=%p shmem=%p\n", mmap_allocator.base, shmem);
   _mulle_mmap_allocator_dump( &mmap_allocator);

   s             = mulle_allocator_strdup( allocator, "VfL Bochum 1848");
   shmem->offset = (size_t) (s - (char *) mmap_allocator.base);
   fprintf( stderr, "child: offset=%zu\n", shmem->offset);
   return( 0);
}


#ifdef _WIN32

static int   run_parent( char *argv0)
{
   struct mulle_mmap_allocator   mmap_allocator;
   struct mulle_allocator        *allocator;
   struct shared_memory          *shmem;
   STARTUPINFOA                  si = { sizeof(si) };
   PROCESS_INFORMATION           pi = { 0 };
   char                          cmdLine[512];
   size_t                        offset;

   mulle_mmap_allocator_init( &mmap_allocator, 8192, mulle_mmap_allocator_shared);
   allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);
   shmem     = mulle_allocator_calloc( allocator, 1, sizeof( struct shared_memory));

   offset = (size_t) ((char *) shmem - (char *) mmap_allocator.base);

   GetModuleFileNameA( NULL, cmdLine, sizeof( cmdLine));
   sprintf( cmdLine + strlen( cmdLine), " --child %llx %zu %llx",
            (unsigned long long)(uintptr_t) mmap_allocator.shared_handle,
            mmap_allocator.capacity,
            (unsigned long long) offset);

   fprintf( stderr, "parent: base=%p shmem=%p\n", mmap_allocator.base, shmem);
   _mulle_mmap_allocator_dump( &mmap_allocator);
   if( ! CreateProcessA( NULL, cmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
   {
      fprintf( stderr, "CreateProcess failed\n");
      return( 1);
   }

   fprintf( stderr, "parent is waiting\n");
   WaitForSingleObject( pi.hProcess, INFINITE);
   CloseHandle( pi.hProcess);
   CloseHandle( pi.hThread);

   printf( "%s\n", (char *) mmap_allocator.base + shmem->offset);

   mulle_mmap_allocator_done( &mmap_allocator);
   return( 0);
}

#else

static int   run_parent( char *argv0)
{
   struct mulle_mmap_allocator   mmap_allocator;
   struct mulle_allocator        *allocator;
   struct shared_memory          *shmem;
   char                          cap_str[32];
   char                          fd_str[32];
   char                          offset_str[32];
   int                           wstatus;
   pid_t                         pid;
   size_t                        offset;

   mulle_mmap_allocator_init( &mmap_allocator, 8192, mulle_mmap_allocator_shared);
   allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);
   shmem     = mulle_allocator_calloc( allocator, 1, sizeof( struct shared_memory));

   offset = (size_t) ((char *) shmem - (char *) mmap_allocator.base);

   snprintf( fd_str,     sizeof( fd_str),     "%llx", (long long)(intptr_t) mmap_allocator.shared_handle);
   snprintf( cap_str,    sizeof( cap_str),    "%zu",  mmap_allocator.capacity);
   snprintf( offset_str, sizeof( offset_str), "%llx", (unsigned long long) offset);

   pid = fork();
   if( pid == 0)
   {
      execl( argv0, argv0, "--child", fd_str, cap_str, offset_str, NULL);
      perror( "execl");
      _exit( 1);
   }

   fprintf( stderr, "parent: base=%p shmem=%p\n", mmap_allocator.base, shmem);
   _mulle_mmap_allocator_dump( &mmap_allocator);
   fprintf( stderr, "parent is waiting\n");

   if( pid < 0 || waitpid( pid, &wstatus, 0) != pid)
   {
      perror( "fork");
      mulle_mmap_allocator_done( &mmap_allocator);
      return( 1);
   }

   if( ! WIFEXITED( wstatus) || WEXITSTATUS( wstatus) != 0)
   {
      fprintf( stderr, "child failed (status %d)\n", wstatus);
      mulle_mmap_allocator_done( &mmap_allocator);
      return( 1);
   }

   printf( "%s\n", (char *) mmap_allocator.base + shmem->offset);

   mulle_mmap_allocator_done( &mmap_allocator);
   return( 0);
}

#endif


int   main( int argc, char *argv[])
{
   if( argc >= 2 && strcmp( argv[1], "--child") == 0)
      return( run_child( argc, argv));
   return( run_parent( argv[0]));
}
