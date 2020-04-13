Design Document for Project 2: User Programs
============================================

## Group Members

* Tom Xie <xie.tom.zy@berkeley.edu>
* John Angeles <johnangeles@berkeley.edu>
* Kevin Li <kevinlishirt@berkeley.edu>
* Coby Zhong <coby.zhong@berkeley.edu>

## Task 1: Buffer Cache

### Data Structures and Functions:

In `inode.c`, we need to create a `struct cache` as defined below, 

``` c
#define MAX_CACHE_BLOCKS 64

int clock_index; // global variable for clock position

struct cache_block { // Note that this must be a write-back cache
    block_sector_t sector;         // Cache block's disk sector    
    struct lock cache_block_lock;        // Cache block operations need to be serialized
    bool valid;                    // valid bit
    bool dirty;                    // dirty bit
    bool clock_bit;                // bit that determines which group it's in for the clock algorithm (young/old)
    uint8_t *data;                    // pointer to data
}

struct cache_block *cache[MAX_CACHE_BLOCKS]; // Cache block array = the cache itself

void init_cache();

off_t cache_read_at(struct inode *inode, void *buffer_, off_t size, off_t offset);

off_t cache_write_at(struct inode *, const void *, off_t size, off_t offset);
```

### Algorithms:
We are going to implement a fully-associative write-back cache. To begin, we need to have a function initialize the cache, which we will do with `init_cache`. This function will initialize each `struct cache_block` in the array of cache blocks which comprises `cache`. 


For `cache_read_at()` and `cache_write_at()`, we must first check if the sector exists in the cache by iterating through the cache array until we find it (or reach the end.) Then,
* **`cache_read_at()`**, 
    * if the sector exists, we just read it into the buffer, otherwise,
    * we run the clock algorithm on the cache until we find an entry to evict, then evict it and load the new entry into its place.
* **`cache_write_at()`**
    * if the sector is in the cache, we directly write to it and set the dirty bit accordingly, otherwise,
    * we find an entry to evict, write it out to disk, then pull the sector we are writing to into the cache and write to it.

Whenever a cache block is read or written to, we set the `clock_bit` flag to true. 

In addition, anytime a page fault is encountered i.e. we find that the sector doesn't exist in the cache, we advance the clock by calling the function `clock_algorithm`, described with pseudocode below,
```c 
/* Chooses next block to evict*/
void clock_algorithm(...) {
    while (victim block not found) {
        if(used bit for current block = 0) {
            replace current block
            advance clock pointer
            break;
        }
        else {
            reset clock_bit
        }
    }
}
```

Also, reading, writing, evicting and loading a block from disk will require acquisition of the `cache_block_lock` of the block, as operations on the same cache block must be serialized.
### Synchronization:
The synchronization for cache operations is handled by the locks that each cache block holds.

The `cache_block_lock` of the cache block handles any attempts at concurrent reads or writes to the same sector in cache. Before any read or write to a block, a thread needs to call `lock_acquire()` on the lock for the cache block it is trying to read or write the data to (or read from).
### Rationale:
We decided to have a goal for simplicity with our design, so we decided to make a fully-associative cache that uses the clock algorithm as the block replacement policy. We went with a fully-associative cache because we thought it was simpler to iterate through the cache to find the cache block we want to use instead of having some function to calculate where to locate a block  for a direct-mapped cache. Although a fully-associative cache is slower than a direct-mapped cache in terms of the amount of comparisons we need to do to locate a block, a fully-associative cache has a higher hit rate since no conflict misses occur, which is something we want to prioritize. 

We chose the clock algorithm because it only requires setting a `clock_bit` and advancing an integer representing the clock hand. We thought of implementing an nth chance clock or implementing LRU to satisfy the replacement policy requirement of having an algorithm that is "at least as good as the 'clock' algorithm," but for LRU, we would have had to utilize some kind of queue to keep track of which cache block was least recently used, and an nth chance clock would replace the clock_bit with some integer counter which would be a bit more complex to keep track of; essentially, both LRU and nth chance clock would require more complex code and more work from the OS.
## Task 2: Extensible Files

### Data Structures and Functions:

#### inode.c/inode.h

We will be getting rid of the contiguous sector format for files and instead implement a system in which an inode will store as many pointers to direct data sectors as possible, and then two pointers to special data sectors. Instead of file data, these data sectors will store a directory of pointers to other sectors. The indirect sector will store pointers to data sectors, whereas the doubly indirect sector will store pointers to sectors which contain pointers to data sectors. The magic value is included to ensure that overflow does not occur.

``` c
# define NUM_DIRECT_SECTORS 124
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

struct inode {
    ...
    struct lock dataCheckIn; // for read/write
    struct condition waitQueue, onDeck; // access queues for read/write
    int queued = 0, onDeck = 0; // sleeping threads for read/write
    int curType = 0, numRWing = 0; // current accessor(s) and their type
}

/* An indirect block that could point to another indirect block or a set of blocks. It will be stored on disk and loaded into memory as needed.
 */
struct indirect_block {
    block_sector_t blocks[128];
}
```

In addition, we will be adding helper functions: 
```c
bool
inode_expand (struct inode *, size_t start, size_t sectors);

void
access (struct inode *, int type);
```
`inode_expand` will expand the inode's data sectors by `sectors` sectors, and return whether or not it was successful in doing so.

`access` will be a helper function limiting access to the inode, explained further in `Algorithms`. `type` is 0 for readers, and 1 for writers.

#### free-map.c/free-map.h

We will be adding a helper function:
```c
size_t
free_map_acquire (size_t cnt, block_sector_t *sectorp);
```
which will attempt to allocate up to `cnt` consecutive sectors, writing the first sector it was able to allocate if it acquired any to `sectorp`, and returning how many it acquired. Returns -1 if it failed to acquire any sectors.

#### bitmap.c

To facilitate the above acquisition, we will add a few helper functions:
```c
size_t
bitmap_lazy_scan (const struct bitmap *b, size_t start, size_t cnt, bool value, size_t *len);

size_t
bitmap_lazy_scan_and_flip (const struct bitmap *b, size_t start, size_t cnt, bool value, size_t *len);
```
which are variants of `bitmap_scan` and `bitmap_scan_and_flip`. Instead of requiring `cnt` consecutive bits, these "lazy" variants will find the first available run of bits that are `VALUE`. Additionally, the latter function will flip the run to `!VALUE`. On fail, `0` will be written to `len` and `BITMAP_ERROR` will be returned.

### Algorithms:

Because we made a change to our inode_disk, we will have to modify many functions in inode.c.

#### byte_to_sector

Instead of adding the offset to the start sector number, we will instead use that to find the appropriate pointer within the data array of the inode itself, or the pointer within the indirect or doubly-indirect blocks. This will require three cases, but we will be able to keep the short circuit of checking if the requested offset is beyond the end of the file by keeping track of the length.

``` c
/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector (const struct inode *inode, off_t pos) {
    ASSERT (inode != NULL);
  /* If the offset is less than the length of the file, then we attempt to find the
   * proper sector */
  if (pos < inode->data.length) {
    if (pos < NUM_DIRECT_SECTORS * BLOCK_SECTOR_SIZE) { // direct pointers
        return (inode->data.direct_sector_ptrs)[pos / BLOCK_SECTOR_SIZE];
    } 
    else if (/* in indirect pointers */) {
        /* Find correct indirect block pointer */
    } 
    else { // doubly-indirect pointers
        /* Find correct doubly-indirect block pointer */
    }
  }
  else 
    return -1;
}
```

#### inode_expand

This helper function will have code similar to the following:
```c
static char zeros[BLOCK_SECTOR_SIZE];
size_t total = 0;
size_t acquired;
size_t runs = 0;
block_sector_t runps[sectors];
size_t run_lens[sectors];
              
while (total < sectors) {
  /* Get next run of free sectors */
  acquired = free_map_acquire (sectors - total, runps[runs]);
  run_lens[runs] = acquired;
  /* Not Included: check for errors.
  * If error, iterate through runps and run_lens and flip back to free, then return false. */
  runs++;
  /* Write zeroes to new sectors */
  size_t i;
  for (i = 0; i < acquired; i++) {
    // Set approriate pointer to sector and write zeroes to disk
            
    total += acquired;
  }
    
}
return true;
```

#### inode_create

Due to the fact that our sectors are no longer guaranteed to be contiguous, we must alter the acquisition of sectors for inode initialization. At a high-level, we will simply ask for continuous runs of free sectors until we have acquired enough sectors.

``` c
/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
    ... // same until sector allocation
    size_t sectors = bytes_to_sectors (length);
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    block_sector_t run_start;
    if (free_map_allocate (sectors, &disk_inode->start)) 
        {
          block_write (fs_device, sector, disk_inode);
          success = inode_expand(sectors); 
        } 
      free (disk_inode);
    }
  return success;
}

```

Note that specifying a initial length of zero does work, and does indeed create a zero byte file.

#### inode_close

A fairly simple change to this function will be needed. If the `open_cnt` is zero, and the inode is removed, we will need to free up all the data sectors, including the indirect and doubly-indirect data sectors, as well as the index pages themselves.

#### access

This function is essentially identical to Midterm 1 Question 3, and ensures that one and only one of these conditions is true at any given time.

  a. No threads are reading or writing this file
  b. One or more threads are reading this file
  c. One thread is writing to this file
  
This is accomplished by having an incompatible thread sleep until the queue ahead of it has cleared.

#### inode_read_at

At the start of this function, we will call `access` to ensure that the thread sleeps if a write is in progress.

Because we will be implementing sparse files in `inode_write_at`, we will need to check if the pointer to the sector we are reading from is actually allocated and within the length of the file. If so, we will automatically write out zeroes instead of reading the sector.

#### inode_write_at

At the start of this function, we will call `access` to ensure that the thread sleeps if a write is in progress.

We will need to check if a write occurs at an absolute offset beyond the current length of the file. If so, we will then need to determine if that places the offset in the direct, indirect, or doubly-indirect region. Based off of that, we will `inode_expand` to expand the requisite number of sectors starting at the offset sector and update the length.

Otherwise, if the sector pointer is zero (indicating it does not exist), manually allocate the sectors we need.

#### free_map_acquire

This function will simply call `bitmap_lazy_scan_and_flip` on its `free_map`, with a mechanism to free the pages if an error occurs, similar to `free_map_allocate`, and return the results.

#### bitmap_lazy

This function will start at the first value that matches, loop forward until it finds a non-matching value, then return how many values matched and where the start was. Returns `BITMAP_ERROR` if no matching values were found.


### Synchronization:

We decided to utilize the solution from Midterm 1, Question 3 to allow multiple readers or one writer to access the inode at a time. 


### Rationale:
We felt it necessary to implement helper functions due to the fact that in Project 2, we tried to cram our implementations into one function, and that led to a lot of headaches regarding debugging. By using helper functions, we will be able to write more clear and concise code.

We do not need a lock for modifying sector pointers because the only time this can happen outside of creation and deletion is due to writing. However, due to the fact that we prevent two writes from occuring simultaneously, we do not need to add a lock.

Our implementation of `inode_write_at` implements sparse files, because we felt that expanding a file shouldn't take more operations than absolutely necessary, and provided that we zero out the index sectors, it should be trivial to identify missing sector pointers.

## Task 3: Subdirectories
### Data Structures and Functions:
**thread.h**
```c
struct thread {
    ...
    char cwd[NAME_MAX + 1]; // holds name of working directory (limited to NAME_MAX)
    ...
}
```
**inode.c**
```c
struct inode_disk {
    ...
    bool directory; // false if a file, true if a direct
    block_sector_t parent; // need a pointer to the parent directory
    ...
}
```
**filesys.c**
```c
base_path(char *abs); // returns the base path of an absolute path 

relative_to_abs(char *rel_path); // returns the absolute path give a relative path
```
### Algorithms:
In our `struct thread`, we need a `char[]` that contains our current working directory (cwd) so that directories can be inherited from parent to child.

We will create two new helper functions `base_path()` and `relative_to_abs()` to implement the newly defined system calls. `base_path()` will return the base file name given an entire absolute path, for example for `/home/foo/bar` it will return `bar`. `relative_to_abs()` will convert a relative filename for the current process to an absolute path by appending the relative path to the `cwd`. This is to maintain consistency so that all paths are absolute. `base_path()` will rely on the `get_next_part()` function put in the spec.



The new syscall implementation will include:
* **`bool chdir (const char *dir)`:** we simply change the process's `cwd` to the given `dir`
* **`bool mkdir (const char *dir)`:** we call `filesys_create` much like the `create` syscall for files, but we change the boolean `directory` of the `inode` to true
* **`bool readdir (int fd, char *name)`:** we just call `dir_readdir()` from `directory.c`
* **`bool isdir (int fd)`:** we look up the directory from the given file descriptor and check the `directory` boolean in the `inode_disk` 
* **`int inumber (int fd)`**: This will look up the directory at the given file descriptor and call `inode_get_inumber()` in `inode.c`

When a directory is removed while in the working directory of a process, we have to prevent new files from being created in it. This entails calling `dir_remove()` appropriately when inspecting the absolute path of the directory when it is deleted.
### Synchronization:
The synchronization for this part is already handled by the buffer cache, as all file system operations must interact with it. Therefore, no synchronization reinforcement is needed for adding subdirectories.
### Rationale:
Each process needs to keep track of its working directory, so the most straight-forward way to do this was to add a `char[]` containing the cwd.

We also needed a way to distinguish between files and directories, so we decided the easiest way to do that was to have an additional boolean in the `inode_disk` struct.

Finally, the two helper functions `base_path()` and `relative_to_abs()` are used because the spec tells us that we can be handed either an absolute path or a relative path, so we decided it would be easier to be able to convert all paths to an absolute path for processing.
## Design Document Additional Questions

### Write-behind

A very general strategy we came up with was to create another thread that will infinitely spin in a while loop while putting itself to sleep one it finishes a loop. 
During a single iteration, the thread will iterate through the cache blocks and check to see if one of them is dirty.  If the cache block is dirty, then we disable interrupts and begin to copy the data from the cache into memory, and then set the cache block to not dirty.

``` c
/* Assume this function has been called upon initializing the cache. */
void write_behind(int sleep_time) {
    while (1) {
        for(int i = 0; i < MAX_CACHE_BLOCKS; i ++) {
            if (cache[i] == NULL) {
                continue;
            } else if (cache[i]->dirty) { // If the cache block is dirty
                intr_disable();
                // Write the data to disk
                intr_enable();
            }
        }
        thread_sleep(sleep_time);
    }
}
```

### Read-ahead

Upon accessing a block within a file or inode, chances are that subsequent blocks within the same inode will be used as well, which means that when we cache a block, we want to cache blocks nearby blocks.

Therefore, in `inode_read_at`, upon calling `byte_to_sector`, we will pull a few surrounding disk blocks and store them in the cache

As a side note, we will pull more subsequent rather than preceeding disk blocks since users may be interested in data that follows after our disk block.

Another more complicated approach yet similar to the clock algorithm would be to store in an array that would take up one disk block a mapping between cache blocks and the number of times they have been accessed.  It would be an array called `num_uses` where each element has a number that represents how many times a certain cache block has been written to or read.  

Upon successfully writing to or reading from a cache block, we use that same index that we have used to access the cache array to index int`num_uses` and increment the count of the element.  

If we delete a cache entry that has a certain high amount of uses, we can "read" from the disk again by preventing the cache block from getting deleted, and will reset the count to 0, and then look for a cache block to evict that has a much lower count.



