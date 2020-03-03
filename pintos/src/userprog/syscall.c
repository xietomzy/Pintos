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
  return ptr != NULL && pagedir_get_page(pd, ptr) && is_user_vaddr(ptr);
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

  if (!is_user_vaddr(args)) { // if esp is invalid
    thread_exit();
  }

  struct thread *t = thread_current();
  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    printf ("%s: exit(%d)\n", &thread_current ()->name, args[1]);
    thread_exit ();
  } else if (args[0] == SYS_PRACTICE) {
    f->eax = args[1] + 1;
    return;
  } else if (args[0] == SYS_HALT) {
    shutdown_power_off();
    NOT_REACHED();
  } else if (args[0] == SYS_WAIT) {
    process_wait(args[1]);
    /*tid_t child_tid = args[1];
    struct thread *cur = thread_current();
    struct list_elem *e;
    struct list children_status = cur->children_status;
    int exit_code;
    for (e = list_begin(&children_status); e != list_end(&children_status); e = list_next(e)) {
      struct child_status *curr_child = list_entry (e, struct child_status, elem);
      if (curr_child->childTid == child_tid) {
        exit_code = process_wait(child_tid);
        break;
      }
    }
    f->eax = exit_code;*/
  } else if (args[0] == SYS_EXEC) {
    // TODO
    f->eax = process_execute(args[1]);
    return;
  } else if (args[0] == SYS_OPEN) {
    if (validate(t->pagedir, args[1])) {
      lock_acquire(&globalFileLock);
      const char* file = (char*) args[1];
      struct file* filePtr = filesys_open(file);
      if (!filePtr) {
        f->eax = -1;
      }
      struct fileDescriptor* fileD = malloc(sizeof(struct fileDescriptor));
      fileD->fileptr = filePtr;
      list_push_back(&t->fileDescriptorList, &fileD->fileElem);
      f->eax = t->fileDesc;
      t->fileDesc += 1;
      lock_release(&globalFileLock);
    }
  } else if (args[0] == SYS_CREATE) {
    if (validate(t->pagedir, args[1])) {
      lock_acquire(&globalFileLock);
      const char* file = (char*) args[1];
      unsigned size = args[2];
      bool success = filesys_create(file, size);
      f->eax = success;
      lock_release(&globalFileLock);
    }
  } else if (args[0] == SYS_REMOVE) {
    if (validate(t->pagedir, args[1])) {
      lock_acquire(&globalFileLock);
      const char* file = (char*) args[1];
      bool success = filesys_remove(file);
      f->eax = success;
      lock_release(&globalFileLock);
    }
  } else if (args[0] == SYS_FILESIZE) {
    lock_acquire(&globalFileLock);
    struct list_elem *e = list_begin (&t->fileDescriptorList);
    int fd = args[1];
    for (int i = 2; i < fd; i++) {
      e = list_next(e);
    }
    struct fileDescriptor* fileD = list_entry(e, struct fileDescriptor, fileElem);
    off_t size = file_length(fileD->fileptr);
    f->eax = size;
    lock_release(&globalFileLock);
  } else if (args[0] == SYS_READ) {
    if (validate(t->pagedir, args[2])) {
      lock_acquire(&globalFileLock);
      int fd = args[1];
      void* buffer = (void*) args[2];
      unsigned sizeB = args[3];
      if (fd == 0) {
        for (int i = 0; i < )
          //input_getc()
      } else {
        struct list_elem *e = list_begin(&t->fileDescriptorList);
        for (int i = 2; i < fd; i++) {
          e = list_next(e);
        }
        struct fileDescriptor * fileD = list_entry(e, struct fileDescriptor, fileElem);
        off_t size = file_read(fileD->fileptr, buffer, sizeB);
        f->eax = size;
        lock_release(&globalFileLock);
      }
    }
  } else if (args[0] == SYS_WRITE) {
    if (validate(t->pagedir, args[2])) {
      lock_acquire(&globalFileLock);
      int fd = args[1];
      const void* buffer = (void*) args[2];
      unsigned sizeB = args[3];
      if (fd == 1) {
        putbuf(buffer, sizeB);
      } else {
        struct list_elem *e = list_begin(&t->fileDescriptorList);
        for (int i = 2; i < fd; i++) {
          e = list_next(e);
        }
        struct fileDescriptor * fileD = list_entry(e, struct fileDescriptor, fileElem);
        off_t size = file_write(fileD->fileptr, buffer, sizeB);
        f->eax = size;
      }
      lock_release(&globalFileLock);
    }
  } else if (args[0] == SYS_SEEK) {
    lock_acquire(&globalFileLock);
    struct list_elem *e = list_begin(&t->fileDescriptorList);
    int fd = args[1];
    for (int i = 2; i < fd; i++) {
      e = list_next(e);
    }
    struct fileDescriptor * fileD = list_entry(e, struct fileDescriptor, fileElem);
    unsigned size = args[2];
    file_seek(fileD->fileptr, size);
    lock_release(&globalFileLock);
  } else if (args[0] == SYS_TELL) {
    lock_acquire(&globalFileLock);
    struct list_elem *e = list_begin(&t->fileDescriptorList);
    int fd = args[1];
    for (int i = 2; i < fd; i++) {
      e = list_next(e);
    }
    struct fileDescriptor * fileD = list_entry(e, struct fileDescriptor, fileElem);
    off_t tell = file_tell(fileD->fileptr);
    f->eax = tell;
    lock_release(&globalFileLock);
  } else if (args[0] == SYS_CLOSE) {
    lock_acquire(&globalFileLock);
    struct list_elem *e = list_begin(&t->fileDescriptorList);
    int fd = args[1];
    for (int i = 2; i < fd; i++) {
      e = list_next(e);
    }
    struct list_elem *removed = list_remove(e);
    struct fileDescriptor * fileD = list_entry(removed, struct fileDescriptor, fileElem);
    file_close(fileD->fileptr);
    lock_release(&globalFileLock);
  }
}
