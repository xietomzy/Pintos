#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Number of direct sectors. */
#define NUM_DIRECT_SECTORS 124

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

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */

    /* Our implementation of inode adds a few more synch tools. */
    struct lock dataCheckIn; // for read/write
    struct lock metadata_lock;
    struct lock resize_lock; // for resizing and writing 
    struct condition waitQueue;
    struct condition onDeck; // access queues for read/write
    int queued;
    int onDeck; // sleeping threads for read/write
    int curType;
    int numRWing; // current accessor(s) and their type
  };

void
access (struct inode *inode, int type)
{

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

  ASSERT (inode != NULL);
  /* If the offset is less than the length of the file, then we attempt to find the
   * proper sector */
  if (pos < inode->data.length) {
    if (pos < direct_bytes) { // direct pointers
      return (inode->data.direct_sector_ptrs)[pos / BLOCK_SECTOR_SIZE];
    } else if (pos < indirect_bytes) { // indirect pointer
      block_sector_t buffer[128];
      cache_read(fs_device, inode->data.ind_blk_ptr, buffer, 0, BLOCK_SECTOR_SIZE);
      // struct indirect_block *ind_blk = inode->data.ind_blk_ptr;
      int blk_index = (pos - direct_bytes) / BLOCK_SECTOR_SIZE;
      return buffer[blk_index];
    } else { // doubly indirect
      block_sector_t buffer[128];
      // struct double_indirect_block *d_ind_blk = inode->data.double_ind_blk_ptr;
      cache_read(fs_device, inode->data.double_ind_blk_ptr, buffer, 0, BLOCK_SECTOR_SIZE);
      int ind_blk_index = (pos - direct_bytes - indirect_bytes) / (128 * BLOCK_SECTOR_SIZE);
      block_sector_t ind_buffer[128];
      // struct indirect_block *ind_blk = (d_ind_blk->indirect_blocks)[ind_blk_index];
      cache_read(fs_device, ind_buffer[ind_blk_index], ind_buffer, 0, BLOCK_SECTOR_SIZE);
      int blk_index = (pos - direct_bytes - indirect_bytes) / BLOCK_SECTOR_SIZE;
      return ind_buffer[blk_index];
    }
  }
  else 
    return -1;
}

/* Helper function for inode_resize. Assumes that INDIRECT_BLOCK_PTR
 * is an indirect block pointer that is already populated with block sectors.
 * It basically release all of the indirect blocks. Does not release indirect_block_ptr! */
void flush_indirect_block(block_sector_t indirect_block_ptr) {
  // This is an indirect_block pointer
  block_sector_t buffer[128];
  memset(buffer, 0, BLOCK_SECTOR_SIZE);
  for (int i = 0; i < 128; i ++) {
    if (buffer[i] != 0) {
      free_map_release(buffer[i], 1);
      buffer[i] = 0;
    }
  }
}

/* Helper function we took inspiration from last year's disc.
 * It will resize the INODE to size SIZE bytes, and sets the length
 * member accordingly. Automatically calls cache_write, but
 * be sure to cache INODE_DISK in the caller!
 * Furthermore, be sure that after acquiring the inode's resizing lock
 * to check whether or not another thread already resized the inode during
 * the period of time in which the current thread saw the need to
 * resize the inode and when the current thread acquired the resize lock.
 * Also frees the lock acquired by the initial inode. */
bool inode_resize(struct inode_disk *inode_disk, off_t size) {
  block_sector_t sector;
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
        inode_resize(inode_disk, inode_disk->length);
        return false;
      }
    }
  }
  // Success case:
  if (inode_disk->ind_blk_ptr == 0 && size < NUM_DIRECT_SECTORS * BLOCK_SECTOR_SIZE) {
    inode_disk->length = size;
    return true;
  }
  block_sector_t buffer[128];
  if (inode_disk->ind_blk_ptr == 0) {
    memset(buffer, 0, BLOCK_SECTOR_SIZE);
    bool status = free_map_allocate(1, &(inode_disk->ind_blk_ptr));
    if (!status) {
      inode_resize(inode_disk, size);
      return false;
    }
  } else { // Read the contents into the buffer
    block_read(fs_device, inode_disk->ind_blk_ptr, buffer);
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
        inode_resize(inode_disk, inode_disk->length);
        return false;
      }
    }
  }
  int size_check_double = NUM_DIRECT_SECTORS * BLOCK_SECTOR_SIZE + (128 * BLOCK_SECTOR_SIZE);
  // Success case:
  if (inode_disk->double_ind_blk_ptr == 0 && size < size_check_double) {
    block_write(fs_device, inode_disk->ind_blk_ptr, buffer);
    inode_disk->length = size;
    return true;
  }

  block_sector_t buffer[128];

  // If we haven't set our indirect block pointer
  if (inode_disk->double_ind_blk_ptr == 0) {
    memset(buffer, 0, BLOCK_SECTOR_SIZE);
    bool status = free_map_allocate(1, &(inode_disk->double_ind_blk_ptr));
    if (!status) {
      inode_resize(inode_disk, inode_disk->length);
      return false;
    }
  } else { // Read the previous contents into the buffer
    block_read(fs_device, inode_disk->double_ind_blk_ptr, buffer);
  }

  for (int i = 0; i < 128; i ++) {
    if (size <= (NUM_DIRECT_SECTORS + 128 + i) * BLOCK_SECTOR_SIZE && buffer[i] != 0) {
      // TODO: Release every block within the indirect block
      flush_indirect_block(buffer[i]);
      free_map_release(buffer[i], 1);
      buffer[i] = 0;
    }

    if (size > (NUM_DIRECT_SECTORS + 128 + i) * BLOCK_SECTOR_SIZE && buffer[i] == 0) {
      bool status = free_map_allocate(1, &(buffer[i]));
      if (!status) {
        inode_resize(inode_disk, inode_disk->length);
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
            inode_resize(inode_disk, inode_disk->length);
            return false;
          }
        }
      }
      block_write(fs_device, buffer[i], &ind_blk_buffer);
    }
  }
  // Success case:
  block_write(fs_device, inode_disk->double_ind_blk_ptr, buffer);
  inode_disk->length = size;
  return true;
}


/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

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
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = 0;
      disk_inode->magic = INODE_MAGIC;
      block_sector_t run_start;
      
      if (inode_resize(disk_inode, length)) 
        {
          // block_write (fs_device, sector, disk_inode);
          cache_write (fs_device, sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
          // if (sectors > 0) 
          //   {
          //     static char zeros[BLOCK_SECTOR_SIZE];
          //     size_t i;
              
          //     for (i = 0; i < sectors; i++) 
          //       // block_write (fs_device, disk_inode->start + i, zeros);
          //       cache_write (fs_device, disk_inode->start + i, zeros, 0, BLOCK_SECTOR_SIZE);
          //   }
          success = true; 
        } 
      free (disk_inode);
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
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  
  /* Initialize locks. */
  lock_init(&(inode->metadata_lock));
  lock_init(&(inode->resize_lock));

  // block_read (fs_device, inode->sector, &inode->data);
  cache_read (fs_device, inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
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
  struct inode_disk *inode_data = &(inode->data);
  for (int i = 0; i < NUM_DIRECT_SECTORS; i ++) {
    free_map_release(inode_data->direct_sector_ptrs[i], 1);
    inode_data->direct_sector_ptrs[i] = 0;
  }
}

/* Closes the indirect pointer. */
void 
inode_close_indir_ptr (block_sector_t indirect_block) {
  block_sector_t buffer[128];
  cache_read(fs_device, indirect_block, buffer, 0, BLOCK_SECTOR_SIZE);
  for (int i = 0; i < 128; i ++) {
    free_map_release(buffer[i], 1);
  }
}

/* Closes the direct pointer. */
void 
inode_close_double_indir_ptr (block_sector_t double_indirect_block) {
  block_sector_t buffer[128];
  cache_read(fs_device, double_indirect_block, buffer, 0, BLOCK_SECTOR_SIZE);
  for (int i = 0; i < 128; i ++) {
    inode_close_indir_ptr(buffer[i]);
    free_map_release(buffer[i], 1);
  }
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
          free_map_release (inode->sector, 1);
          // free_map_release (inode->data.start,
          //                   bytes_to_sectors (inode->data.length)); 
          // MAY NEED TO ZERO CHECK THESE FUNCTIONS
          inode_close_dir_ptrs(inode);
          inode_close_indir_ptr(inode->data.ind_blk_ptr);
          inode_close_double_indir_ptr(inode->data.double_ind_blk_ptr);
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
      // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      //   {
      //     /* Read full sector directly into caller's buffer. */
      //     //block_read (fs_device, sector_idx, buffer + bytes_read);
      //   }
      // else 
      //   {
      //     /* Read sector into bounce buffer, then partially copy
      //        into caller's buffer. */
      //     if (bounce == NULL) 
      //       {
      //         bounce = malloc (BLOCK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }
      //     block_read (fs_device, sector_idx, bounce);
      //     memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
      //   }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  //free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  //uint8_t *bounce = NULL;

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
      // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      //   {
      //     /* Write full sector directly to disk. */
      //     block_write (fs_device, sector_idx, buffer + bytes_written);
      //   }
      // else 
      //   {
      //     /* We need a bounce buffer. */
      //     if (bounce == NULL) 
      //       {
      //         bounce = malloc (BLOCK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }

      //     /* If the sector contains data before or after the chunk
      //        we're writing, then we need to read in the sector
      //        first.  Otherwise we start with a sector of all zeros. */
      //     if (sector_ofs > 0 || chunk_size < sector_left) 
      //       block_read (fs_device, sector_idx, bounce);
      //     else
      //       memset (bounce, 0, BLOCK_SECTOR_SIZE);
      //     memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
      //     block_write (fs_device, sector_idx, bounce);
      //   }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  //free (bounce);

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
  return inode->data.length;
}
