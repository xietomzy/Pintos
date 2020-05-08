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

    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    block_sector_t data;                /* Pointer to the inode disk. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */

    /* Our implementation of inode adds a few more synch tools. */
    struct lock dataCheckIn; // for read/write
    struct lock metadata;
    struct condition waitQueue;
    struct condition onDeckQueue; // access queues for read/write
    int queued; // num queued threads
    int onDeck; // sleeping threads for read/write
    int curType; // 0 = reading, 1 = writing
    int numRWing; // current accessor(s)

    uint32_t unused[96];

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

  int return_val = -1;

  ASSERT (inode != NULL);

  struct inode_disk *data = malloc(BLOCK_SECTOR_SIZE);
  if (data == NULL) {
    PANIC("Failure to load inode_disk");
  }
  cache_read(fs_device, inode->data, data, 0, BLOCK_SECTOR_SIZE);

  /* If the offset is less than the length of the file, then we attempt to find the
   * proper sector */
  if (pos < data->length) {
    if (pos < direct_bytes) { // direct pointers
      return_val = (data->direct_sector_ptrs)[pos / BLOCK_SECTOR_SIZE];
      goto cleanup;
    } else if (pos < indirect_bytes) { // indirect pointer
      block_sector_t (*buffer)[128] = malloc(128 * sizeof (block_sector_t));
      if (data->ind_blk_ptr == 0) {
        PANIC("No indirect block allocated");
      }
      cache_read(fs_device, data->ind_blk_ptr, buffer, 0, BLOCK_SECTOR_SIZE);
      // struct indirect_block *ind_blk = inode->data.ind_blk_ptr;
      int blk_index = (pos - direct_bytes) / BLOCK_SECTOR_SIZE;
      return_val = (*buffer)[blk_index];
      free(buffer);
      goto cleanup;
    } else { // doubly indirect
      block_sector_t (*buffer)[128] = malloc(128 * sizeof (block_sector_t));
      if (data->double_ind_blk_ptr == 0) {
        PANIC("No doubly direct block allocated");
      }
      cache_read(fs_device, data->double_ind_blk_ptr, buffer, 0, BLOCK_SECTOR_SIZE);
      int ind_blk_index = (pos - direct_bytes - indirect_bytes) / (128 * BLOCK_SECTOR_SIZE);
      block_sector_t (*ind_buffer)[128] = malloc(128 * sizeof(block_sector_t));
      cache_read(fs_device, *ind_buffer[ind_blk_index], ind_buffer, 0, BLOCK_SECTOR_SIZE);
      int blk_index = (pos - direct_bytes - indirect_bytes) / BLOCK_SECTOR_SIZE;
      return_val = *ind_buffer[blk_index];
      free(ind_buffer);
      free(buffer);
      goto cleanup;
    }
  }
  cleanup:
    free(data);
    return return_val;
}

/* Helper function for inode_resize. Assumes that INDIRECT_BLOCK_PTR
 * is an indirect block pointer that is already populated with block sectors.
 * It basically release all of the indirect blocks. Does not release indirect_block_ptr! */
void flush_indirect_block(block_sector_t indirect_block_ptr) {
  // This is an indirect_block pointer
  block_sector_t buffer[128];
  memset(buffer, 0, BLOCK_SECTOR_SIZE);
  cache_read(fs_device, indirect_block_ptr, buffer, 0, BLOCK_SECTOR_SIZE);
  for (int i = 0; i < 128; i ++) {
    if (buffer[i] != 0) {
      free_map_release(buffer[i], 1);
      buffer[i] = 0;
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
  struct inode_disk *inode_disk = malloc(BLOCK_SECTOR_SIZE);
  ASSERT (sizeof(*inode_disk) == BLOCK_SECTOR_SIZE);
  cache_read(fs_device, inode->data, inode_disk, 0, BLOCK_SECTOR_SIZE);

  /* Perform iteration up to the number of direct sectors */
  for (int i = 0; i < NUM_DIRECT_SECTORS; i ++) {
    if ((size <= BLOCK_SECTOR_SIZE * i) &&
        (inode_disk->direct_sector_ptrs[i] != 0)) {
      free_map_release(inode_disk->direct_sector_ptrs[i], 1);
      inode_disk->direct_sector_ptrs[i] = 0;
    }
    // Somehow, if the previous blocks haven't been allocated, do so here!
    if ((size > BLOCK_SECTOR_SIZE * i) &&
        (inode_disk->direct_sector_ptrs[i] == 0)) {
      bool status = free_map_allocate(1, &(inode_disk->direct_sector_ptrs[i]));
      if (!status) {
        // if we fail to resize, shrink back
        inode_resize(inode, inode_disk->length);
        return false;
      }
    }
  }
  // Success case:
  if (inode_disk->ind_blk_ptr == 0 && size < NUM_DIRECT_SECTORS * BLOCK_SECTOR_SIZE) {
    inode_disk->length = size;
    cache_write(fs_device, inode->data, inode_disk, 0, BLOCK_SECTOR_SIZE);
    return true;
  }
  block_sector_t buffer[128];
  if (inode_disk->ind_blk_ptr == 0) {
    memset(buffer, 0, BLOCK_SECTOR_SIZE);
    bool status = free_map_allocate(1, &(inode_disk->ind_blk_ptr));
    if (!status) {
      inode_resize(inode, size);
      return false;
    }
  } else { // Read the contents into the buffer
    cache_read(fs_device, inode_disk->ind_blk_ptr, buffer, 0, BLOCK_SECTOR_SIZE);
  }

  for (int i = 0; i < 128; i ++) {
    if (size <= (NUM_DIRECT_SECTORS + i) * BLOCK_SECTOR_SIZE && buffer[i] != 0) {
      free_map_release(buffer[i], 1);
      buffer[i] = 0;
    }
    // Somehow, if the previous blocks haven't been allocated, do so here!
    if ((size > (NUM_DIRECT_SECTORS + i) * BLOCK_SECTOR_SIZE) && buffer[i] == 0) {
      bool status = free_map_allocate(1, &(buffer[i]));
      if (!status) {
        inode_resize(inode, inode_disk->length);
        return false;
      }
    }
  }

  /* Success case: doubly indirect blocks */
  int size_check_double = NUM_DIRECT_SECTORS * BLOCK_SECTOR_SIZE + (128 * BLOCK_SECTOR_SIZE);
  // Success case:
  if (inode_disk->double_ind_blk_ptr == 0 && size < size_check_double) {
    inode_disk->length = size;
    cache_write(fs_device, inode_disk->ind_blk_ptr, buffer, 0, BLOCK_SECTOR_SIZE);
    cache_write(fs_device, inode->data, inode_disk, 0, BLOCK_SECTOR_SIZE);
    return true;
  }

  // block_sector_t buffer[128];

  // If we haven't set our indirect block pointer
  if (inode_disk->double_ind_blk_ptr == 0) {
    memset(buffer, 0, BLOCK_SECTOR_SIZE);
    bool status = free_map_allocate(1, &(inode_disk->double_ind_blk_ptr));
    if (!status) {
      inode_resize(inode, inode_disk->length);
      return false;
    }
  } else { // Read the previous contents into the buffer
    cache_read(fs_device, inode_disk->double_ind_blk_ptr, buffer, 0, BLOCK_SECTOR_SIZE);
  }

  // Iterate through pointers to pointer blocks
  for (int i = 0; i < 128; i ++) {
    if (size <= (NUM_DIRECT_SECTORS + 128 + i) * BLOCK_SECTOR_SIZE && buffer[i] != 0) {
      // Release every block within the indirect block
      flush_indirect_block(buffer[i]);
      free_map_release(buffer[i], 1);
      buffer[i] = 0;
    }

    if (size > (NUM_DIRECT_SECTORS + 128 + i) * BLOCK_SECTOR_SIZE && buffer[i] == 0) {
      bool status = free_map_allocate(1, &(buffer[i]));
      if (!status) {
        inode_resize(inode, inode_disk->length);
        return false;
      }
      block_sector_t ind_blk_buffer[128];
      // Then allocate the appropriate number of blocks
      for (int j = 0; j < 128; j ++) {
        if (size <= ((NUM_DIRECT_SECTORS + 128 + j) * BLOCK_SECTOR_SIZE) && ind_blk_buffer[j] == 0) {
          free_map_release(buffer[j], 1);
          buffer[i] = 0;
        }
        if ((size > (NUM_DIRECT_SECTORS + 128 + j) * BLOCK_SECTOR_SIZE) && ind_blk_buffer[j] == 0) {
          bool status = free_map_allocate(1, &(buffer[j]));
          if (!status) {
            inode_resize(inode, inode_disk->length);
            return false;
          }
        }
      }
      cache_write(fs_device, buffer[i], &ind_blk_buffer, 0, BLOCK_SECTOR_SIZE);
    }
  }
  // Success case:
  inode_disk->length = size;
  cache_write(fs_device, inode_disk->double_ind_blk_ptr, buffer, 0, BLOCK_SECTOR_SIZE);
  cache_write(fs_device, inode->data, inode_disk, 0, BLOCK_SECTOR_SIZE);
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
      node->magic = INODE_MAGIC;
      node->sector = sector;
      bool data_status = free_map_allocate(1, &(node->data));
  
      if (!data_status) {
        return false;
      }

      if (inode_resize(node, length))
        {
          cache_write (fs_device, sector, node, 0, BLOCK_SECTOR_SIZE);
          success = true;
        }
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
  if (inode == NULL)
    return NULL;

  cache_read (fs_device, sector, inode, 0, BLOCK_SECTOR_SIZE);

  /* Initialize. */
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  lock_init(&(inode->dataCheckIn));
  lock_init(&(inode->metadata));
  cond_init(&(inode->waitQueue));
  cond_init(&(inode->onDeckQueue));
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
  struct inode_disk inode_disk;
  cache_read(fs_device, inode->data, &inode_disk, 0, BLOCK_SECTOR_SIZE);
  for (int i = 0; i < NUM_DIRECT_SECTORS; i ++) {
    if (inode_disk.direct_sector_ptrs[i] != 0) {
      free_map_release(inode_disk.direct_sector_ptrs[i], 1);
      inode_disk.direct_sector_ptrs[i] = 0;
    }
  }
  cache_write(fs_device, inode->data, &inode_disk, 0, BLOCK_SECTOR_SIZE);
}

/* Closes the indirect pointer, and sets the inode's indirect_block pointer to 0. */
void
inode_close_indir_ptr (struct inode *inode) {
  block_sector_t buffer[128];
  struct inode_disk inode_disk;
  cache_read(fs_device, inode->data, &inode_disk, 0, BLOCK_SECTOR_SIZE);

  if (inode_disk.ind_blk_ptr == 0) {
    return;
  }
  cache_read(fs_device, inode_disk.ind_blk_ptr, buffer, 0, BLOCK_SECTOR_SIZE);
  for (int i = 0; i < 128; i ++) {
    if (buffer[i] != 0) {
      free_map_release(buffer[i], 1);
    }
  }
  inode_disk.ind_blk_ptr = 0;
  cache_write(fs_device, inode->data, &inode_disk, 0, BLOCK_SECTOR_SIZE);
}

/* Frees up every single pointer within block, which we assume to be a pointer to an indirect pointer. */
void 
close_indir_ptr (block_sector_t block) {
  block_sector_t buffer[128];
  cache_read(fs_device, block, buffer, 0, BLOCK_SECTOR_SIZE);
  for (int i = 0; i < 128; i ++) {
    if (buffer[i] != 0) {
      free_map_release(buffer[i], 1);
    }
  }
}

/* Closes the doubly indirect pointer. */
void
inode_close_double_indir_ptr (struct inode *inode) {
  block_sector_t buffer[128];
  struct inode_disk inode_disk;
  cache_read(fs_device, inode->data, buffer, 0, BLOCK_SECTOR_SIZE);
  if (inode_disk.double_ind_blk_ptr == 0) {
    return;
  }
  cache_read(fs_device, inode_disk.double_ind_blk_ptr, buffer, 0, BLOCK_SECTOR_SIZE);
  for (int i = 0; i < 128; i ++) {
    if (buffer[i] != 0) {
      close_indir_ptr(buffer[i]);
      free_map_release(buffer[i], 1);
    }
  }
  inode_disk.double_ind_blk_ptr = 0;
  cache_write(fs_device, inode->data, &inode_disk, 0, BLOCK_SECTOR_SIZE);
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
  //uint8_t *bounce = NULL;

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

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

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
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk *disk = malloc(BLOCK_SECTOR_SIZE);
  cache_read(fs_device, inode->data, disk, 0, BLOCK_SECTOR_SIZE);
  off_t length = disk->length;
  free(disk);
  return length;
}
