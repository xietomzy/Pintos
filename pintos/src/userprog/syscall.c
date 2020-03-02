#include "userprog/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "devices/shutdown.h"


struct lock globalFileLock;

struct fileDescriptor
  {
    struct list_elem fileElem;
    struct file *fileptr;
  };

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

  /* printf("System call number: %d\n", args[0]); */

  if (!is_user_vaddr(args)) { // if esp is invalid
    thread_exit();
  }

  struct thread *t = thread_current();
  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    printf ("%s: exit(%d)\n", &thread_current ()->name, args[1]);
    thread_exit();
  } else if (args[0] == SYS_OPEN) {
    if (validate(t->pagedir, args[1])) {
      lock_acquire(&globalFileLock);
      struct file* filePtr = filesys_open(args[1]);
      struct fileDescriptor* fileD = malloc(sizeof(struct fileDescriptor));
      fileD->fileptr = filePtr;
      list_push_back(&t->fileDescriptorList, &fileD->fileElem);
      t->fileDesc += 1;
      f->eax = filePtr;
      lock_release(&globalFileLock);
    }
  } else if (args[0] == SYS_CREATE) { 
    if (validate(t->pagedir, args[1])) {
      lock_acquire(&globalFileLock);
      bool success = file_write(args[1], args[2], args[3]);
      f->eax = success;
      lock_release(&globalFileLock);
    }

  if (args[0] == SYS_HALT) {
    shutdown_power_off();
  }

  if (args[0] == SYS_EXEC) {
    tid_t tid = process_execute(args[1]);
  }

  if (args[0] == SYS_PRACTICE) {
    args[1] += 1; 
  }
}
