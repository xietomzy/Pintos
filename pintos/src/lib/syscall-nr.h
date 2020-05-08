#ifndef __LIB_SYSCALL_NR_H
#define __LIB_SYSCALL_NR_H

/* System call numbers. */
enum 
  {
    /* Projects 2 and later. */
    SYS_HALT,                   /* Halt the operating system. */
    SYS_EXIT,                   /* Terminate this process. */
    SYS_EXEC,                   /* Start another process. */
    SYS_WAIT,                   /* Wait for a child process to die. */
    SYS_CREATE,                 /* Create a file. */
    SYS_REMOVE,                 /* Delete a file. */
    SYS_OPEN,                   /* Open a file. */
    SYS_FILESIZE,               /* Obtain a file's size. */
    SYS_READ,                   /* Read from a file. */
    SYS_WRITE,                  /* Write to a file. */
    SYS_SEEK,                   /* Change position in a file. */
    SYS_TELL,                   /* Report current position in a file. */
    SYS_CLOSE,                  /* Close a file. */
    SYS_PRACTICE,               /* Returns arg incremented by 1 */

    /* Project 4 only. */
    SYS_CHDIR,                  /* Change the current directory. */
    SYS_MKDIR,                  /* Create a directory. */
    SYS_READDIR,                /* Reads a directory entry. */
    SYS_ISDIR,                  /* Tests if a fd represents a directory. */
    SYS_INUMBER,                 /* Returns the inode number for a fd. */

    /* Project 3 and optionally project 4. */
    SYS_RESET_CACHE,            /* Flushes the cache. */
    SYS_NUM_CACHE_HITS,         /* The number of cache hits before resetting the cache. */
    SYS_NUM_CACHE_ACCESSES,     /* The number of cache accesses before resetting the cache. */
    SYS_NUM_DEVICE_READS,       /* The number of file system device reads. */
    SYS_NUM_DEVICE_WRITES,      /* The number of file system device writes. */
    SYS_MMAP,                   /* Map a file into memory. */
    SYS_MUNMAP                 /* Remove a memory mapping. */


    
  };

#endif /* lib/syscall-nr.h */
