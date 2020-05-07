#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

bool rel_or_abs(const char *name);
static int get_next_part (char part[NAME_MAX + 1], const char **srcp);
void * filesys_open_helper(struct dir *directory, const char *name, bool *f_or_d);

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
next call will return the next file name part. Returns 1 if successful, 0 at
end of string, -1 for a too-long file name part. */
static int
get_next_part (char part[NAME_MAX + 1], const char **srcp) {
  const char *src = *srcp;
  char *dst = part;
  /* Skip leading slashes. If it’s all slashes, we’re done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;
  /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';
  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  cache_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  cache_flush ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* 0 for relative, 1 for absolute*/
bool rel_or_abs(const char *name) {
  if (*name == '/') { // if absolute
    return true;
  }
  return false;
}

void * filesys_open_helper(struct dir *directory, const char *name, bool *f_or_d) {
  struct dir *dir = directory;
  if (dir == NULL) {
    return NULL;
  }
  char name_buffer[NAME_MAX + 1];
  struct inode *inode = NULL;
  bool success;
  struct file *file;
  char **srcp = &name; // name pointer for get_next_part
  while (get_next_part(name_buffer, srcp) == 1) {
    success = dir_lookup(dir, name_buffer, &inode); // check if directory/file exists in dir
    if (!success) { // if dir/file doesn't exist, return NULL
      return NULL;
    }

    file = file_open(inode); // try to open file
    if (file != NULL) { // if path has file, immediately return the file
      if (thread_current()->cwd != NULL) {
        thread_current()->cwd = dir; // set cwd
      }
      dir_close(dir);
      *f_or_d = 0;
      return (void *) file;
    }
    dir_close(dir);
    dir = dir_open(inode); // try to open directory
    if (dir != NULL) { // if we get a directory, continue looking into the directory
      if (**srcp == '\0') { // if last thing in path is a directory, we return a directory
        if (thread_current()->cwd != NULL) {
          thread_current()->cwd = dir; // set cwd
        }
        *f_or_d = 1;
        return (void *) dir;
      }
    }
  }
  dir_close(dir);
  return NULL;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
void *
filesys_open (const char *name, bool *f_or_d)
{
  //struct dir *dir = dir_open_root ();
  /* if given absolute path */
  if (rel_or_abs(name) || thread_current()->cwd == NULL) {
    return filesys_open_helper(dir_open_root(), name, f_or_d);
  } else { // if given relative path
    return filesys_open_helper(dir_reopen(thread_current()->cwd), name, f_or_d);
  }
  return NULL;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

bool filesys_chdir(const char *name) {
  bool dummy;
  struct dir *result;
  if (rel_or_abs(name)) { // if absolute
    result = (struct dir *) filesys_open_helper(dir_open_root(), name, &dummy);
  } else { // if relative
    result = (struct dir *) filesys_open_helper(dir_reopen(thread_current()->cwd), name, &dummy);
  }

  if (result != NULL) {
    return true;
  }
  return false;
}

bool filesys_mkdir_helper(struct dir* directory, const char *name) {
  struct dir *dir = directory;
  if (dir == NULL) {
    return false;
  }
  char name_buffer[NAME_MAX + 1];
  struct inode *inode = NULL;
  //bool success;
  char **srcp = &name; // name pointer for get_next_part
  struct dir *prev_dir;
  while (get_next_part(name_buffer, srcp) == 1) {
    dir_lookup(dir, name_buffer, &inode); // check if directory/file exists in dir
    dir_close(dir);
    prev_dir = dir;
    dir = dir_open(inode); // try to open directory
    if (**srcp == '\0') { // if last thing in path is a directory, we return a directory
      if (dir != NULL) {
        return false;
      }
      block_sector_t inode_sector = 0;
      bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, 2)
                  && dir_add (prev_dir, name_buffer, inode_sector));
      if (!success && inode_sector != 0) {
        free_map_release (inode_sector, 1);
      }
      dir_add (dir, "..", inode_get_inumber(dir_get_inode(prev_dir))); // add parent directory
      dir_add (dir, ".", inode_sector); // add current directory
      inode_sector = 0;
      return true;
    }
  }
  dir_close(dir);
  return false;
}

bool filesys_mkdir(const char *name) {
  bool result = NULL;
  if (rel_or_abs(name)) {
    result = filesys_mkdir_helper(dir_open_root(), name);
  } else {
    result = filesys_mkdir_helper(dir_reopen(thread_current()->cwd), name);
  }

  return result;
}
