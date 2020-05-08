#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Number of direct sectors. */
#define NUM_DIRECT_SECTORS 124

void checkout(struct inode *inode);
void flush_indirect_block(block_sector_t indirect_block_ptr);
bool inode_resize(struct inode *inode, off_t size);
void inode_close_dir_ptrs (struct inode *inode);
void inode_close_indir_ptr (struct inode *inode);
void close_indir_ptr (block_sector_t block);
void inode_close_double_indir_ptr (struct inode *inode);

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* A couple of synchronization global locks. */
struct lock open_inodes_lock;
struct lock global_freemap_lock;
struct condition monitor_file_deny;

/* An indirect block that could point to another indirect block or a set of blocks.
 * It will be stored on disk and loaded into memory as needed.
 */
struct indirect_block {
  block_sector_t blocks[128];
};

struct double_indirect_block {
  block_sector_t indirect_blocks[128];
};

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    block_sector_t direct_sector_ptrs[NUM_DIRECT_SECTORS];        /* Direct data sectors. */
    block_sector_t ind_blk_ptr;         /* Indirect pointers. */
    block_sector_t double_ind_blk_ptr;  /* Doubly Indirect pointers. */
    unsigned magic;                     /* Magic number. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

static void
zero_block (block_sector_t block) {
  off_t divisions = 8;
  ASSERT (BLOCK_SECTOR_SIZE % (sizeof(block_sector_t) * divisions) == 0);
  size_t buf_len = BLOCK_SECTOR_SIZE / (sizeof(block_sector_t) * divisions);
  block_sector_t zero_buf[buf_len];
  memset(zero_buf, 0, BLOCK_SECTOR_SIZE / divisions);
  for (int i = 0; i < divisions; i++) {
    cache_write(fs_device, block, &zero_buf, i * buf_len * sizeof(block_sector_t), buf_len * sizeof(block_sector_t));
  }
}

/* In-memory inode. */
struct inode
  {
    /* Begin metadata protection */
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    block_sector_t data;                /* Pointer to the inode disk. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    /* End metadata protection. */

    struct lock metadata;
    struct lock resize;

    /* Tools to allow concurrent reads and serialized writes. */
    struct lock dataCheckIn; // for read/write
    struct condition waitQueue;
    struct condition onDeckQueue; // access queues for read/write
    int queued; // num queued threads
    int onDeck; // sleeping threads for read/write
    int curType; // 0 = reading, 1 = writing
    int numRWing; // current accessor(s)

    uint32_t unused[90];

    unsigned magic;                     /* Magic number. */
  };

/* Called by access methods to this inode before actually
   accessing or modifying any data within the inode. 
   Type 0 is reading, and type 1 is writing. */
void
access (struct inode *inode, int type)
{
  // Check-in
  lock_acquire(&(inode->dataCheckIn));

  if (inode->queued + inode->onDeck > 0)
  {
    inode->queued++;
    cond_wait(&(inode->waitQueue), &(inode->dataCheckIn));
    inode->queued--;
  }

  while ((inode->numRWing > 0) && (type == 1 || inode->curType == 1))
  {
    (inode->onDeck)++;
    cond_wait(&(inode->onDeckQueue), &(inode->dataCheckIn));
    (inode->onDeck)--;
  }

  // Complete check-in
  lock_release(&(inode->dataCheckIn));
}

/* Called by access methods to this inode after
   all data is accessed. Failure to call this method
   will result in this inode being inaccessible. */
void
checkout (struct inode *inode)
{
  lock_acquire(&(inode->dataCheckIn));
  (inode->numRWing) --;
  cond_signal(&(inode->onDeckQueue), &(inode->dataCheckIn));
  lock_release(&(inode->dataCheckIn));
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS.
   We modified this because of our new inode implementation. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  /* Number of bytes that only direct pointers can handle. */
  int direct_bytes = NUM_DIRECT_SECTORS * BLOCK_SECTOR_SIZE;
  /* Number of bytes up to the indirect pointer case. */
  int indirect_bytes = NUM_DIRECT_SECTORS * BLOCK_SECTOR_SIZE +
                             128 * BLOCK_SECTOR_SIZE;

  block_sector_t return_val;

  ASSERT (inode != NULL);
  ASSERT (pos >= 0);

  off_t length;
  cache_read(fs_device, inode->data, &length, 0, sizeof(off_t));

  // If the offset is not within the file, return -1
  if (pos >= length) {
    return -1;
  }
  // Direct pointers
  if (pos < direct_bytes) {
    off_t block_pos = sizeof(off_t) + pos/BLOCK_SECTOR_SIZE * sizeof(block_sector_t);
    cache_read(fs_device, inode->data, &return_val, block_pos, sizeof(block_sector_t));
    return return_val;
  }
  // Indirect pointers
  if (pos < indirect_bytes) { // indirect pointer
    off_t ind_blk_pos = sizeof(off_t) + NUM_DIRECT_SECTORS * sizeof(block_sector_t);
    block_sector_t ind_blk_ptr;
    cache_read(fs_device, inode->data, &ind_blk_ptr, ind_blk_pos, sizeof(block_sector_t));
    if (ind_blk_ptr == 0) {
      PANIC("File claims to have indirect block, but it is not initialized");
    }

    off_t blk_index = (pos - direct_bytes) / BLOCK_SECTOR_SIZE * sizeof(block_sector_t);
    cache_read(fs_device, ind_blk_ptr, &return_val, blk_index, sizeof(block_sector_t));
    return return_val;
  }
  // doubly indirect
  off_t dbl_ind_blk_pos = sizeof(off_t) + (NUM_DIRECT_SECTORS + 1) * sizeof(block_sector_t);
  block_sector_t dbl_ind_blk_ptr;
  cache_read(fs_device, inode->data, &dbl_ind_blk_ptr, dbl_ind_blk_pos, sizeof(block_sector_t));
  if (dbl_ind_blk_ptr == 0) {
    PANIC("File claims to have doubly indirect block, but it is not initialized");
  }

  off_t next_blk_index = (pos - direct_bytes - indirect_bytes) / (128 * BLOCK_SECTOR_SIZE) * sizeof(block_sector_t);
  block_sector_t next_blk_ptr;
  cache_read(fs_device, dbl_ind_blk_ptr, &next_blk_ptr, next_blk_index, sizeof(block_sector_t));
  if (next_blk_ptr == 0) {
    return -1;
  }

  off_t skipped_dbl_bytes = (next_blk_index * 128 * BLOCK_SECTOR_SIZE);
  off_t blk_index = (pos - direct_bytes - indirect_bytes - skipped_dbl_bytes) / BLOCK_SECTOR_SIZE * sizeof(block_sector_t);
  cache_read(fs_device, next_blk_ptr, &return_val, blk_index, sizeof(block_sector_t));
  return return_val;
}

/* Helper function for inode_resize. Assumes that INDIRECT_BLOCK_PTR
 * is an indirect block pointer that is already populated with block sectors.
 * It basically release all of the indirect blocks. Does not release indirect_block_ptr! */
void flush_indirect_block(block_sector_t indirect_block_ptr) {
  for (int i = 0; i < 128; i ++) {
    off_t byte_off = i * sizeof(block_sector_t);
    block_sector_t ptr;
    cache_read(fs_device, indirect_block_ptr, &ptr, byte_off, sizeof(ptr));
    if (ptr != 0) {
      free_map_release(ptr, 1);
      ptr = 0;
      cache_write(fs_device, indirect_block_ptr, &ptr, byte_off, sizeof(ptr));
    }
  }
}

/* Helper function adapted from last year's discussion.
 * It will resize the INODE to size SIZE bytes, and sets the length
 * member accordingly. Automatically calls cache_write, but
 * be sure to cache inode in the caller!
 * Furthermore, be sure that after acquiring the inode's resizing lock
 * to check whether or not another thread already resized the inode during
 * the period of time in which the current thread saw the need to
 * resize the inode and when the current thread acquired the resize lock.
 * Also frees the lock acquired by the initial inode. */
bool inode_resize(struct inode *inode, off_t size) {
  ASSERT (lock_held_by_current_thread(&(inode->resize)));
  ASSERT (inode->curType == 1);

  // Check if another thread already resized before we could start resizing
  if (inode_length (inode) >= size) {
    return true;
  }

  off_t cur_len;
  cache_read(fs_device, inode->data, &cur_len, 0, sizeof(cur_len));

  /* Perform iteration up to the number of direct sectors */
  for (int i = 0; i < NUM_DIRECT_SECTORS; i ++) {
    block_sector_t dir_blk;
    off_t dir_blk_off = i * sizeof(block_sector_t) + sizeof(off_t);
    cache_read(fs_device, inode->data, &dir_blk, dir_blk_off, sizeof(block_sector_t));
    if ((size <= BLOCK_SECTOR_SIZE * i) &&
        (dir_blk != 0)) {
      free_map_release(dir_blk, 1);
      dir_blk = 0;
      cache_write(fs_device, inode->data, &dir_blk, dir_blk_off, sizeof(block_sector_t));
    }
    // Somehow, if the previous blocks haven't been allocated, do so here!
    if ((size > BLOCK_SECTOR_SIZE * i) &&
        (dir_blk == 0)) {
      bool status = free_map_allocate(1, &dir_blk);
      if (!status) {
        // if we fail to resize, shrink back
        inode_resize(inode, cur_len);
        return false;
      }
      cache_write(fs_device, inode->data, &dir_blk, dir_blk_off, sizeof(block_sector_t));
    }
  }

  /* Success case: indirect blocks */
  block_sector_t ind_blk_ptr;
  off_t end_dir_off = NUM_DIRECT_SECTORS * sizeof(block_sector_t) + sizeof(off_t);
  cache_read(fs_device, inode->data, &ind_blk_ptr, end_dir_off, sizeof(block_sector_t));

  // If we're not dealing with indirect blocks, and the file does not have indirect blocks, exit now
  if (ind_blk_ptr == 0 && size < NUM_DIRECT_SECTORS * BLOCK_SECTOR_SIZE) {
    cache_write(fs_device, inode->data, &size, 0, sizeof(off_t));
    return true;
  }
  // Allocate a new sector for the indirect pointers
  if (ind_blk_ptr == 0) {
    bool status = free_map_allocate(1, &(ind_blk_ptr));
    if (!status) {
      inode_resize(inode, size);
      return false;
    }
    cache_write(fs_device, inode->data, &ind_blk_ptr, end_dir_off, sizeof(ind_blk_ptr));
    // Zero the new block
    zero_block(ind_blk_ptr);
  }

  for (int i = 0; i < 128; i ++) {
    block_sector_t ind_ptr;
    off_t ind_blk_off = i * sizeof(block_sector_t);
    cache_read(fs_device, ind_blk_ptr, &ind_ptr, ind_blk_off, sizeof(block_sector_t));
    if (size <= (NUM_DIRECT_SECTORS + i) * BLOCK_SECTOR_SIZE && ind_ptr != 0) {
      free_map_release(ind_ptr, 1);
      ind_ptr = 0;
      cache_write(fs_device, ind_blk_ptr, &ind_ptr, ind_blk_off, sizeof(block_sector_t));
    }
    // Somehow, if the previous blocks haven't been allocated, do so here!
    if ((size > (NUM_DIRECT_SECTORS + i) * BLOCK_SECTOR_SIZE) && ind_ptr == 0) {
      bool status = free_map_allocate(1, &ind_ptr);
      if (!status) {
        inode_resize(inode, cur_len);
        return false;
      }
      cache_write(fs_device, ind_blk_ptr, &ind_ptr, ind_blk_off, sizeof(block_sector_t));
    }
  }

  /* Success case: doubly indirect blocks */
  int size_check_double = NUM_DIRECT_SECTORS * BLOCK_SECTOR_SIZE + (128 * BLOCK_SECTOR_SIZE);
  block_sector_t blk1_ptr;
  off_t dbl_ptr_off = end_dir_off + sizeof(block_sector_t);
  cache_read(fs_device, inode->data, &blk1_ptr, dbl_ptr_off, sizeof(block_sector_t));

  // If we're not dealing with doubly indirect blocks, and the file does not have doubly indirect blocks, exit now
  if (blk1_ptr == 0 && size < size_check_double) {
    cache_write(fs_device, inode->data, &size, 0, sizeof(off_t));
    return true;
  }

  // If we haven't set our doubly indirect block pointer
  if (blk1_ptr == 0) {
    bool status = free_map_allocate(1, &blk1_ptr);
    if (!status) {
      inode_resize(inode, cur_len);
      return false;
    }
    cache_write(fs_device, inode->data, &blk1_ptr, dbl_ptr_off, sizeof(block_sector_t));
    zero_block(blk1_ptr);
  }

  // Iterate through pointers to pointer blocks
  for (int i = 0; i < 128; i ++) {
    block_sector_t blk2_ptr;
    off_t blk1_off = i * sizeof(block_sector_t);
    cache_read(fs_device, blk1_ptr, &blk2_ptr, blk1_off, sizeof(blk2_ptr));

    if (size <= (NUM_DIRECT_SECTORS + (i + 1) * 128) * BLOCK_SECTOR_SIZE && blk2_ptr != 0) {
      // Release every block within the indirect block
      flush_indirect_block(blk2_ptr);
      free_map_release(blk2_ptr, 1);
      blk2_ptr = 0;
      cache_write(fs_device, blk1_ptr, &blk2_ptr, blk1_off, sizeof(blk2_ptr));
    }

    if (size > (NUM_DIRECT_SECTORS + (i + 1) * 128) * BLOCK_SECTOR_SIZE && blk2_ptr == 0) {
      bool status = free_map_allocate(1, &blk2_ptr);
      if (!status) {
        inode_resize(inode, cur_len);
        return false;
      }
      cache_write(fs_device, blk1_ptr, &blk2_ptr, blk1_off, sizeof(blk2_ptr));
      // Then allocate the appropriate number of blocks
      for (int j = 0; j < 128; j ++) {
        off_t final_off = j * sizeof(block_sector_t);
        block_sector_t final_ptr;
        cache_read(fs_device, blk2_ptr, &final_ptr, final_off, sizeof(final_ptr));
        if ((size <= (NUM_DIRECT_SECTORS + (j + 1) * 128) * BLOCK_SECTOR_SIZE) && final_ptr == 0) {
          free_map_release(final_ptr, 1);
          final_ptr = 0;
          cache_write(fs_device, blk2_ptr, &final_ptr, final_off, sizeof(final_ptr));
        }
        if ((size > (NUM_DIRECT_SECTORS + (j + 1) * 128) * BLOCK_SECTOR_SIZE) && final_ptr == 0) {
          bool status = free_map_allocate(1, &final_ptr);
          if (!status) {
            inode_resize(inode, cur_len);
            return false;
          }
          cache_write(fs_device, blk2_ptr, &final_ptr, final_off, sizeof(final_ptr));
        }
      }
    }
  }
  // Success case:
  cur_len = size;
  cache_write(fs_device, inode->data, &cur_len, 0, sizeof(cur_len));
  return true;
}

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  /* Initialize the global locks. */
  lock_init(&open_inodes_lock);
  lock_init(&global_freemap_lock);
  cond_init(&monitor_file_deny);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode *node = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  // printf("\nSize of inode: %d\n", sizeof *node);
  ASSERT (sizeof *node == BLOCK_SECTOR_SIZE);

  node = calloc (1, sizeof *node);
  if (node != NULL)
    {
      /* Initialize. */
      node->open_cnt = 1;
      node->deny_write_cnt = 0;
      node->removed = false;

      lock_init(&(node->dataCheckIn));
      lock_init(&(node->metadata));
      lock_init(&(node->resize));
      cond_init(&(node->waitQueue));
      cond_init(&(node->onDeckQueue));
      node->magic = INODE_MAGIC;
      node->sector = sector;
      bool data_status = free_map_allocate(1, &(node->data));
  
      if (!data_status) {
        return false;
      }

      access(node, 1);
      lock_acquire(&(node->resize));
      if (inode_resize(node, length))
        {
          cache_write (fs_device, sector, node, 0, BLOCK_SECTOR_SIZE);
          success = true;
        }
      lock_release(&(node->resize));
      checkout(node);
      free (node);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  lock_acquire(&open_inodes_lock);
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          lock_release(&open_inodes_lock);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL) {
    lock_release(&open_inodes_lock);
    return NULL;
  }

  cache_read (fs_device, sector, inode, 0, BLOCK_SECTOR_SIZE);

  list_push_front (&open_inodes, &(inode->elem));

  lock_release(&open_inodes_lock);
  return inode; 
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL) {
    lock_acquire(&(inode->metadata));
    inode->open_cnt++;
    lock_release(&(inode->metadata));
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes all of the direct pointers. */
void
inode_close_dir_ptrs (struct inode *inode) {
  for (int i = 0; i < NUM_DIRECT_SECTORS; i ++) {
    block_sector_t dir_ptr;
    off_t offset = sizeof(off_t) + i * sizeof(block_sector_t);
    cache_read(fs_device, inode->data, &dir_ptr, offset, sizeof(dir_ptr));
    if (dir_ptr != 0) {
      free_map_release(dir_ptr, 1);
      dir_ptr = 0;
      cache_write(fs_device, inode->data, &dir_ptr, offset, sizeof(dir_ptr));
    }
  }
}

/* Closes the indirect pointer, and sets the inode's indirect_block pointer to 0. */
void
inode_close_indir_ptr (struct inode *inode) {
  block_sector_t ind_blk_ptr;
  off_t offset = sizeof(off_t) + NUM_DIRECT_SECTORS * sizeof(block_sector_t);
  cache_read(fs_device, inode->data, &ind_blk_ptr, offset, sizeof(ind_blk_ptr));

  if (ind_blk_ptr == 0) {
    return;
  }

  close_indir_ptr (ind_blk_ptr);
  ind_blk_ptr = 0;
  cache_write(fs_device, inode->data, &ind_blk_ptr, 0, sizeof(ind_blk_ptr));
}

/* Frees up every single pointer within block, which we assume to be a pointer to an indirect pointer. */
void 
close_indir_ptr (block_sector_t block) {
  block_sector_t blk_ptr;
  for (int i = 0; i < 128; i ++) {
    off_t offset = i * sizeof(block_sector_t);
    cache_read(fs_device, block, &blk_ptr, offset, sizeof(blk_ptr));
    if (blk_ptr != 0) {
      free_map_release(blk_ptr, 1);
      blk_ptr = 0;
      cache_read(fs_device, block, &blk_ptr, offset, sizeof(blk_ptr));
    }
  }
}

/* Closes the doubly indirect pointer. */
void
inode_close_double_indir_ptr (struct inode *inode) {
  block_sector_t blk1_ptr;
  off_t offset = sizeof(off_t) + (NUM_DIRECT_SECTORS + 1) * sizeof(block_sector_t);
  cache_read(fs_device, inode->data, &blk1_ptr, offset, sizeof(blk1_ptr));
  if (blk1_ptr == 0) {
    return;
  }

  block_sector_t blk2_ptr;
  for (int i = 0; i < 128; i ++) {
    offset = i * sizeof(block_sector_t);
    cache_read(fs_device, blk1_ptr, &blk2_ptr, offset, sizeof(blk2_ptr));
    if (blk2_ptr != 0) {
      close_indir_ptr(blk2_ptr);
      free_map_release(blk2_ptr, 1);
      blk2_ptr = 0;
      cache_write(fs_device, blk1_ptr, &blk2_ptr, offset, sizeof(blk2_ptr));
    }
  }
  blk1_ptr = 0;
  cache_write(fs_device, inode->data, &blk1_ptr, offset, sizeof(blk1_ptr));
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  // Write this inode out to disk
  cache_write(fs_device, inode->sector, inode, 0, sizeof(inode));

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {

          // free_map_release (inode->data.start,
          //                   bytes_to_sectors (inode->data.length));
          // MAY NEED TO ZERO CHECK THESE FUNCTIONS
          inode_close_dir_ptrs(inode);
          inode_close_indir_ptr(inode);
          inode_close_double_indir_ptr(inode);
          free_map_release(inode->data, 1);
          free_map_release (inode->sector, 1);
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  access(inode, 0);
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;


      cache_read (fs_device, sector_idx, buffer + bytes_read, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  
  checkout(inode);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   A write at the end of the file extends the inode.
   */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  access(inode, 1);
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  // Check for resize
  if (offset + size > inode_length (inode)) {
    lock_acquire (&(inode->resize));
    bool success = inode_resize (inode, offset + size);
    lock_release (&(inode->resize));
    if (!success) {
      return 0;
    }
  }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      int min_left = BLOCK_SECTOR_SIZE - sector_ofs;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_write(fs_device, sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  checkout(inode);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  // TODO: Sleep until all writers finish if there are any
  lock_acquire(&(inode->metadata));
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&(inode->metadata));
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  lock_acquire(&(inode->metadata));
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&(inode->metadata));
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  off_t length;
  cache_read(fs_device, inode->data, &length, 0, sizeof(off_t));
  return length;
}
