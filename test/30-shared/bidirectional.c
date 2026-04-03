#include "include.h"

#include <mulle-mmapallocator/mulle-mmapallocator.h>
#include <mulle-thread/mulle-thread.h>
#include <mulle-fifo/mulle-fifo.h>
#include <mulle-sprintf/mulle-sprintf.h>

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
# include <windows.h>
#else
# include <unistd.h>
# include <sys/types.h>
# include <sys/wait.h>
#endif


struct shared_memory
{
   struct mulle__pointerfifo32   child_parent;
   struct mulle__pointerfifo32   parent_child;
};


#ifndef _WIN32

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

   if( ! fork())
   {
      fprintf( stderr, "child is reading and writing shared memory\n");

      for(; i < 32;)
      {
         s = _mulle__pointerfifo32_read( &shmem->parent_child);
         if( ! s)
            continue;

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

#else

int   main( int argc, char *argv[])
{
   struct mulle_allocator        *allocator;
   struct mulle_mmap_allocator   mmap_allocator;
   struct shared_memory          *shmem;
   char                          *s;
   char                          *s2;
   int                           i;

   if( argc >= 2 && strcmp( argv[1], "--child") == 0)
   {
      mulle_mmap_file_t   handle;
      size_t              capacity;
      void                *base;

      handle   = (mulle_mmap_file_t)(uintptr_t) strtoull( argv[2], NULL, 16);
      capacity = (size_t) strtoull( argv[3], NULL, 10);
      base     = (void *)(uintptr_t) strtoull( argv[4], NULL, 16);

      mulle_mmap_allocator_attach( &mmap_allocator, handle, capacity, base);
      allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);
      shmem     = (struct shared_memory *)(uintptr_t) strtoull( argv[5], NULL, 16);

      fprintf( stderr, "child is reading and writing shared memory\n");

      i = 0;
      for(; i < 32;)
      {
         s = _mulle__pointerfifo32_read( &shmem->parent_child);
         if( ! s)
            continue;

         fprintf( stderr, "child received %s\n", s);

         mulle_allocator_asprintf( allocator, &s2, "c->p %d", i++);

         fprintf( stderr, "child sends %s\n", s2);
         _mulle__pointerfifo32_write( &shmem->child_parent, s2);

         fprintf( stderr, "child frees %s\n", s);
         mulle_allocator_free( allocator, s);
      }

      fprintf( stderr, "child is exiting\n");
      return( 0);
   }

   mulle_mmap_allocator_init( &mmap_allocator, 8192, mulle_mmap_allocator_shared);
   allocator = mulle_mmap_allocator_as_allocator( &mmap_allocator);
   shmem     = mulle_allocator_calloc( allocator, 1, sizeof( struct shared_memory));

   _mulle__pointerfifo32_init( &shmem->child_parent);
   _mulle__pointerfifo32_init( &shmem->parent_child);

   {
      STARTUPINFOA        si = { sizeof(si) };
      PROCESS_INFORMATION pi = { 0 };
      char                cmdLine[512];

      GetModuleFileNameA( NULL, cmdLine, sizeof(cmdLine));
      sprintf( cmdLine + strlen( cmdLine), " --child %llx %zu %llx %llx",
               (unsigned long long)(uintptr_t) mmap_allocator.shared_handle,
               mmap_allocator.capacity,
               (unsigned long long)(uintptr_t) mmap_allocator.base,
               (unsigned long long)(uintptr_t) shmem);

      if( ! CreateProcessA( NULL, cmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
      {
         fprintf( stderr, "CreateProcess failed\n");
         return( 1);
      }

      fprintf( stderr, "parent is reading and writing shared memory\n");

      i = 0;
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

      WaitForSingleObject( pi.hProcess, INFINITE);
      CloseHandle( pi.hProcess);
      CloseHandle( pi.hThread);
   }

   mulle_mmap_allocator_done( &mmap_allocator);
   return( 0);
}

#endif
