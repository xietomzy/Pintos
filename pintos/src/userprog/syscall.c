#include "userprog/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <threads/malloc.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "userprog/process.h"


struct lock globalFileLock;

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  lock_init(&globalFileLock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool validate(uint32_t *pd, const void* ptr) {
  bool result;
  for (int i = 0; i < 4; i++) { // check each byte of ptr
    result = ptr + i != NULL && is_user_vaddr(ptr + i) && pagedir_get_page(pd, ptr + i) != NULL && ptr + i + 4 < PHYS_BASE;
    if (!result) {
      return false;
    }
  }
  return result;
  
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  // printf("System call number: %d\n", args[0]);

  struct thread *t = thread_current();
  if (!validate(t->pagedir, args)) { // if esp is invalid
    thread_exit();
  }

  if (args[0] == SYS_EXIT) {
     f->eax = args[1];
     if(!is_user_vaddr(&args[1])) {
        thread_current()->self_status->exit_code = -1;
        thread_exit();
    } else {
        thread_current()->self_status->exit_code = args[1];
        thread_exit();
    }
  } else if (args[0] == SYS_PRACTICE) {
    f->eax = args[1] + 1;
    return;
  } else if (args[0] == SYS_HALT) {
    shutdown_power_off();
    NOT_REACHED();
  } else if (args[0] == SYS_WAIT) {
    int wait = process_wait(args[1]);
    f->eax = wait;
  } else if (args[0] == SYS_EXEC) {
    if (validate(t->pagedir, &args[1]) && validate(t->pagedir, args[1])) {
      f->eax = process_execute(args[1]);
      return;
    } else {
      f->eax = -1;
      thread_exit();
    }
  } else if (args[0] == SYS_OPEN) {
    if (validate(t->pagedir, args[1])) {
      lock_acquire(&globalFileLock);
      const char* file = (char*) args[1];
      struct file* filePtr = filesys_open(file);
      if (!filePtr) {
        f->eax = -1;
      } else {
        struct fileDescriptor* fileD = malloc(sizeof(struct fileDescriptor));
        fileD->fileptr = filePtr;
        fileD->fd = t->fileDesc;
        list_push_back(&t->fileDescriptorList, &fileD->fileElem);
        f->eax = t->fileDesc;
        t->fileDesc += 1;
      }
      lock_release(&globalFileLock);
    } else {
      f->eax = -1;
      thread_exit();
    }
  } else if (args[0] == SYS_CREATE) {
    const char* file = (char*) args[1];
    if (validate(t->pagedir, file)) {
      lock_acquire(&globalFileLock);
      unsigned size = args[2];
      bool success = filesys_create(file, size);
      f->eax = success;
      lock_release(&globalFileLock);
    } else {
      f->eax = -1;
      thread_exit();
    }

  } else if (args[0] == SYS_REMOVE) {
    if (validate(t->pagedir, args[1])) {
      lock_acquire(&globalFileLock);
      const char* file = (char*) args[1];
      bool success = filesys_remove(file);
      f->eax = success;
      lock_release(&globalFileLock);
    } else {
      f->eax = -1;
    }

  } else if (args[0] == SYS_FILESIZE) {
    lock_acquire(&globalFileLock);
    struct list_elem *e = list_begin (&t->fileDescriptorList);
    int fd = args[1];
    for (int i = 2; i < fd - 1; i++) {
      e = list_next(e);
    }
    if (fd >= t->fileDesc || fd < 0) {
      f->eax = -1;
    } else {
      struct fileDescriptor* fileD = list_entry(e, struct fileDescriptor, fileElem);
      if (fileD->fd != fd) {
        f->eax = -1;
      } else {
        off_t size = file_length(fileD->fileptr);
        f->eax = size;
      }
    }
    lock_release(&globalFileLock);
  } else if (args[0] == SYS_READ) {
    if (validate(t->pagedir, args[2])) {
      lock_acquire(&globalFileLock);
      int fd = args[1];
      void* buffer = (void*) args[2];
      unsigned sizeB = args[3];
      if (fd >= t->fileDesc || fd < 0) {
        f->eax = -1;
        thread_exit();
      } else {
        if (fd == 0) {
            uint8_t *input = (uint8_t *) buffer; // stdin
            int bytes_read = 0;
            while (bytes_read < sizeB) {
              input[bytes_read] = input_getc();
              if (input[bytes_read + 1] == '\n') {
                break;
              }
            }
            f->eax = bytes_read;
        } else {
          struct list_elem *e = list_begin(&t->fileDescriptorList);
          for (int i = 2; i < fd - 1; i++) {
            e = list_next(e);
          }
          struct fileDescriptor * fileD = list_entry(e, struct fileDescriptor, fileElem);
          if (fileD->fd != fd) {
            f->eax = -1;
          } else {
            off_t size = file_read(fileD->fileptr, buffer, sizeB);
            f->eax = size;
          }
        }
      }
      lock_release(&globalFileLock);
    } else {
      f->eax = -1;
      thread_exit();
    }
  } else if (args[0] == SYS_WRITE) {
    if (validate(t->pagedir, args[2])) {
      int fd = args[1];
      const void* buffer = (void*) args[2];
      unsigned sizeB = args[3];
      if (fd >= t->fileDesc || fd < 0) {
        f->eax = -1;
      } else {
        if (fd == 1) {
	        putbuf((void*)args[2], args[3]);
        } else {
          struct list_elem *e = list_begin(&t->fileDescriptorList);
          for (int i = 2; i < fd - 1; i++) {
            e = list_next(e);
          }
          struct fileDescriptor * fileD = list_entry(e, struct fileDescriptor, fileElem);
          if (fileD->fd != fd) {
            f->eax = -1;
          } else {
            lock_acquire(&globalFileLock);
            f->eax = file_write(fileD->fileptr, (void*) args[2], args[3]);
            lock_release(&globalFileLock);
          }
        }
      }
    } else {
      f->eax = -1;
      thread_exit();
    }
  } else if (args[0] == SYS_SEEK) {
    lock_acquire(&globalFileLock);
    struct list_elem *e = list_begin(&t->fileDescriptorList);
    int fd = args[1];
    if (fd >= t->fileDesc || fd < 0) {
      f->eax = -1;
    } else {
      for (int i = 2; i < fd - 1; i++) {
        e = list_next(e);
      }
      struct fileDescriptor * fileD = list_entry(e, struct fileDescriptor, fileElem);
      if (fileD->fd != fd) {
        f->eax = -1;
      } else {
        unsigned size = args[2];
        file_seek(fileD->fileptr, size);
      }
    }
    lock_release(&globalFileLock);
  } else if (args[0] == SYS_TELL) {
    lock_acquire(&globalFileLock);
    struct list_elem *e = list_begin(&t->fileDescriptorList);
    int fd = args[1];
    if (fd >= t->fileDesc || fd < 0) {
      f->eax = -1;
    } else {
      for (int i = 2; i < fd - 1; i++) {
        e = list_next(e);
      }
      struct fileDescriptor * fileD = list_entry(e, struct fileDescriptor, fileElem);
      if (fileD->fd != fd) {
        f->eax = -1;
      } else {
        off_t tell = file_tell(fileD->fileptr);
        f->eax = tell;
      }
    }
    lock_release(&globalFileLock);
  } else if (args[0] == SYS_CLOSE) {
    lock_acquire(&globalFileLock);
    struct list_elem *e = list_begin(&t->fileDescriptorList);
    int fd = args[1];
    if (fd >= t->fileDesc || fd < 0) {
      f->eax = -1;
    } else {
      for (int i = 2; i < fd - 1; i++) {
        e = list_next(e);
      }
      struct fileDescriptor * fileD = list_entry(e, struct fileDescriptor, fileElem);
          
      if (fileD->fd != fd) {
        f->eax = -1;
      } else {
        list_remove(e);
        file_close(fileD->fileptr);
      }
    }

    lock_release(&globalFileLock);
  }
}
