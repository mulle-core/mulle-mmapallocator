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


struct shared_memory
{
   char   *s;
};


static int   run_child( int argc, char *argv[])
{
   struct mulle_mmap_allocator   mmap_allocator;
   struct mulle_allocator        *allocator;
   struct shared_memory          *shmem;
   mulle_mmap_file_t             handle;
   size_t                        capacity;
   void                          *base;

#ifdef _WIN32
   handle = (mulle_mmap_file_t)(uintptr_t) strtoull( argv[2], NULL, 16);
#else
   handle = (mulle_mmap_file_t)(intptr_t) strtoll( argv[2], NULL, 16);
#endif
   capacity = (size_t) strtoull( argv[3], NULL, 10);
   base     = (void *)(uintptr_t) strtoull( argv[4], NULL, 16);
   shmem    = (struct shared_memory *)(uintptr_t) strtoull( argv[5], NULL, 16);

   mulle_mmap_allocator_attach( &mmap_allocator, handle, capacity, base);
   allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);

   fprintf( stderr, "child: base requested=%p got=%p\n", base, mmap_allocator.base);
   _mulle_mmap_allocator_dump( &mmap_allocator);
   shmem->s = mulle_allocator_strdup( allocator, "VfL Bochum 1848");
   fprintf( stderr, "child: shmem->s=%p\n", (void *) shmem->s);
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

   mulle_mmap_allocator_init( &mmap_allocator, 8192, mulle_mmap_allocator_shared);
   allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);
   shmem     = mulle_allocator_calloc( allocator, 1, sizeof( struct shared_memory));

   GetModuleFileNameA( NULL, cmdLine, sizeof( cmdLine));
   sprintf( cmdLine + strlen( cmdLine), " --child %llx %zu %llx %llx",
            (unsigned long long)(uintptr_t) mmap_allocator.shared_handle,
            mmap_allocator.capacity,
            (unsigned long long)(uintptr_t) mmap_allocator.base,
            (unsigned long long)(uintptr_t) shmem);

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

   fprintf( stderr, "parent: shmem->s=%p\n", (void *) shmem->s);
   fprintf( stderr, "parent is reading shared memory\n");
   printf( "%s\n", shmem->s);

   mulle_mmap_allocator_done( &mmap_allocator);
   return( 0);
}

#else

static int   run_parent( char *argv0)
{
   struct mulle_mmap_allocator   mmap_allocator;
   struct mulle_allocator        *allocator;
   struct shared_memory          *shmem;
   char                          fd_str[32];
   char                          cap_str[32];
   char                          base_str[32];
   char                          shmem_str[32];
   int                           wstatus;
   pid_t                         pid;

   mulle_mmap_allocator_init( &mmap_allocator, 8192, mulle_mmap_allocator_shared);
   allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);
   shmem     = mulle_allocator_calloc( allocator, 1, sizeof( struct shared_memory));

   snprintf( fd_str,    sizeof( fd_str),    "%llx", (long long)(intptr_t) mmap_allocator.shared_handle);
   snprintf( cap_str,   sizeof( cap_str),   "%zu",  mmap_allocator.capacity);
   snprintf( base_str,  sizeof( base_str),  "%llx", (unsigned long long)(uintptr_t) mmap_allocator.base);
   snprintf( shmem_str, sizeof( shmem_str), "%llx", (unsigned long long)(uintptr_t) shmem);

   pid = fork();
   if( pid == 0)
   {
      execl( argv0, argv0, "--child", fd_str, cap_str, base_str, shmem_str, NULL);
      perror( "execl");
      _exit( 1);
   }

   fprintf( stderr, "parent: base=%p shmem=%p\n", mmap_allocator.base, shmem);
   _mulle_mmap_allocator_dump( &mmap_allocator);
   fprintf( stderr, "parent is waiting\n");
   waitpid( pid, &wstatus, 0);

   fprintf( stderr, "parent: shmem->s=%p\n", (void *) shmem->s);
   fprintf( stderr, "parent is reading shared memory\n");
   printf( "%s\n", shmem->s);

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
