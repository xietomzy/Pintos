# CS162 Project 3 Final Report
Members: Kevin Li, John Angeles, Tom Xie, Coby Zheng
GSI: Neil K.
## Task 1: Buffer Cache
### Changes made to original design doc:
For task 1, we made only a few minor changes. One change that we made is that we decided to use a different eviction algorithm. In our design doc, we planned on using a clock algorithm but decided to use LRU instead since implementing LRU would be much easier. Another change that we made for task 1 was that we added a global lock to the cache in `cache.c` called `cache_lock`. This global lock is necessary for synchronization and is used while scanning the cache to determine if it is a cache hit. Originally, in our design doc, we only had a lock in `cache_block` called `cache_block_lock` but realized that it was insufficient for synchronization.


## Task 2: Extensible Files
### Changes Made to Original Design Doc
For task 2, we made quite a lot of changes, but one of particular interest is our `inode_resize` function.
### Data Structures
#### Inode.c/Inode.h
- `inode_disk` Remained exactly the same, but `struct inode` changed a lot.
```c
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

    /* Tools to allow concurrent reads and serialized writes. Protected by dataCheckIn. */
    struct lock dataCheckIn; // for read/write
    struct condition waitQueue;
    struct condition onDeckQueue; // access queues for read/write
    int queued; // num queued threads
    int onDeck; // sleeping threads for read/write
    int curType; // 0 = reading, 1 = writing
    int numRWing; // current accessor(s)
    struct condition waitActiveWriters; // To force file_deny_write to wait for all writers to finish

    struct lock dir_lock;               /* Lock only used if inode refers to a directory; size = 24 bytes*/
    bool is_dir;                        /* 0 if not dir, 1 otw */

    uint8_t unused[86 * 4 - sizeof(struct lock) - sizeof(bool)];

    unsigned magic;                     /* Magic number. */

  };
```
- `struct inode` no longer has a pointer to memory which contains `inode_disk`. Instead, it has a pointer to the block containing the `inode_disk` struct.  - `struct inode` also contains the sector of where it resides on disk so that it can be written back when necessary.
- We also added both direct and indirect block structs.
```c
struct indirect_block {
  block_sector_t blocks[128];
};

struct double_indirect_block {
  block_sector_t indirect_blocks[128];
};
```
- However, due to a misunderstanding with regards to bounce buffers, these structs do not get used, and should have been removed.

### Algorithms
#### `byte_to_sector`
- This one had more or less didn't change, but because `data` in `struct inode` is a `block_sector_t`, we just called `cache_read` to populate a dynamically-allocated buffer to allow us to fetch the correct sector.
#### `inode_resize`
- `inode_expand` is actually from the design doc in our project called `inode_resize`.  It was based off of last year's discussion that allows us to properly resize an inode, and the best part is that when we mess up, we can recursively call the function to rollback any progress we have made on a faulty call.
- The signature takes in an `inode` to be resized, and a `size` that we want to resize the inode to. We really liked this function because it's quite flexible given that we pass in a single size, and that the caller doesn't have to worry about the internal details of how many blocks they should allocate.
- A helper function `inode_resize_no_check` was added to handle the business logic, since during creation we needed to resize the inode when no other thread could have access to it. Although theoretically this is unneeded if the thread simply goes through the semantics of accessing the node properly, we felt that this was unecessary.
#### `inode_init`
- Not mentioned in the design doc, but we just initialize some global synchronization primitives.
### `inode_create`
- We didn't really follow our design doc for this algorithm, but on a high level the algorithm is simple.  We create a buffer to temporarily store our new inode, populate its initial members, call `inode_resize` which thankfully does all of the logic for us in terms of to what size we should have our new inode be, and write our inode to disk before freeing our buffer.
### `inode_open`
- It didn't really change much from the design doc apart.  We just check the `open_inodes` list, and initialize our synchronization variables.
### `inode_close`
- In our deisgn doc, we mentioned that this was a pretty simple change.  It is conceptually but it's code-dense, and we ended up building three helper functions to close the direct and indirect pointers to make the code a but cleaner.
### `inode_read_at` and `inode_write_at`
- Not much of a change, but we did have to make sure that multiple reads and only one write can happen at a time to the same sector.  All of that logic happens with two new functions based off of midterm 1, called `access` and `checkout`.  A high-level idea is that anytime we want to read or write to an inode, each thread will be forced to call `access` and leave with a call to `checkout` that handles all of the synchronization logic.  It's really nice because having both of those functions mean that we can keep our code very clean.
### `free_map_acquire`
- We ended up ditching this function in favor of iteratively acquiring a block at a time, and having our resize function handle rollback

We did not have to make chanes to other functions outside of `inode.c/inode.h` worth noting.



## Task 3: Subdirectories
### Changes made to original design doc:
For task 3, we had to make quite a few changes to our design. One issue that we hadn't fully flushed out in our design was how to handle `dir_remove` when another process has an inode open. When a process has an inode open, `dir_remove` did not actually remove the directory until that every process has closed the inode. To handle this...  Another change we made was to store special directory entries, such as `.` and `..`. This change drastically helped improve/simplify path resolution within our code. Also, we had to make some changes to `struct inode` in order to synchronize directory operations. In `struct inode`, we added a lock that would be utilized everytime an inode refers to a directory. Finally, we modified one of the fields we created in `struct thread` to be a pointer instead of an array. We did this since the field, `cwd`, could be longer than `NAME_MAX` and we hadn't accounted for that in our design doc.

## Reflection

As a group, we felt that this project was very challenging. Even though we tried to start early and split the work into equal chunks, the project still took a long time to complete. There were many times during the project that we would overlook some small errors and details, and it would cost the group a lot of time to debug these small error. By the end of the project, everyone had a pretty good understanding of each other's part since we had to constantly proofread and understand the code in order to help debug. If we were to do another project, I think we should pay more attention to the small details since this ended up costing us a lot of time. Also, I think if we were to do this project again, we would spend a little more time working on the design document and being more thorough with the design. 

## Student Testing Report
### `my-test-1`: The increased hit rate test
#### Description
This test is to test a higher hit rate from a cache that is already populated with blocks.
#### Overview of Testing Mechanics
The test itself is simple. We start off with a cold cache, read from a file, measure the hit rate, and then re-read again from the same file with hopes of getting a higher hit rate. The test is roughly as follows:

```c 
void test_main (void) {
    a. Declare useful local variables such as fd
    b. Reset the cache and check that the number of accesses and hits is zeroed
    c. Create and open a file
    d. Read byte-by-byte
    e. Measure the number of cache hits/accesses to get a rough hit rate
    f. Close and reopen the file
    g. Read byte-by-byte
    h. Measure the number of cache hits/accesses to get a rough hit rate
}
```

We've provided a couple of system calls to help make this test.

```c 
/* Resets the cache. */
void reset_cache(void);

/* Returns the number of cache accesses ever since the last call to 
 * reset_cache().
 */
int number_cache_accesses(void); 

/* Returns the number of cache hits ever since the last call to reset_cache()
 */
int number_cache_hits(void);
```

Because we weren't able to perform float division, we just printed out the raw number of accesses and hits for the first and second rounds of reading byte-by-byte from the file, and the user can just use a calculator to find the increased hit rate.
#### Output

```
Copying tests/filesys/extended/my-test-1 to scratch partition...
Copying tests/filesys/extended/tar to scratch partition...
qemu-system-i386 -device isa-debug-exit -hda /tmp/bRziQZiGjk.dsk -hdb tmp.dsk -m 4 -net none -nographic -monitor null
PiLo hda1
Loading..............
Kernel command line: -q -f extract run my-test-1
Pintos booting with 3,968 kB RAM...
367 pages available in kernel pool.
367 pages available in user pool.
Calibrating timer...  73,830,400 loops/s.
hda: 1,008 sectors (504 kB), model "QM00001", serial "QEMU HARDDISK"
hda1: 224 sectors (112 kB), Pintos OS kernel (20)
hda2: 201 sectors (100 kB), Pintos scratch (22)
hdb: 5,040 sectors (2 MB), model "QM00002", serial "QEMU HARDDISK"
hdb1: 4,096 sectors (2 MB), Pintos file system (21)
filesys: using hdb1
scratch: using hda2
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'my-test-1' into the file system...
Putting 'tar' into the file system...
Erasing ustar archive...
Executing 'my-test-1':
(my-test-1) begin
(my-test-1) Number of initial cache accesses: 0
(my-test-1) Number of initial cache hits: 0
(my-test-1) create "logfile"
(my-test-1) open "logfile"
(my-test-1) Number of first set of cache accesses: 12395
(my-test-1) Number of first set of cache hits: 12380
(my-test-1) open "logfile"
(my-test-1) Number of second set of cache accesses: 12299
(my-test-1) Number of second set of cache hits: 12299
(my-test-1) end
my-test-1: exit(0)
Execution of 'my-test-1' complete.
Timer: 174 ticks
Thread: 37 idle ticks, 66 kernel ticks, 71 user ticks
hdb1 (filesys): 260 reads, 249 writes
hda2 (scratch): 200 reads, 2 writes
Console: 1394 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```

#### Non-trivial bugs:
1. There could be synchronization issues.  If we created two threads A and B where A called `number_cache_accesses` and B other called `reset_cache`, then we could have the number of cache accesses be 0. Then we would have a division by zero if we decided to calculate a float-based hit rate. There could also be integer overflow if there are too many accesses.
2. If we had two threads A and B where A called `reset_cache` while B was about to increment the number of cache accesses from x-1 to x, then `reset_cache` will have resetted the number of cache accesses to 0, and B could be immediately re-scheduled to set the number of cache accesses to x, mitigating the work done by `reset_cache`.

### `my-test-2`: Coalescing Reads and Writes
#### Description
The main idea is that the number of reads and writes to the file system will be around 128 even though we will read and write 64 Kilobytes byte-by-byte. 
#### Overview of Testing Mechanics
We had to make a few system calls to help facilitate this test.
```c 
/* Returns the number of device reads ever since we started the program. */
long long number_device_reads (void);

/* Returns the number of device writes ever since we started the program. */
long long number_device_writes (void);
```
Then. on a high level, we just perform a bunch of writes, check the number of device writes. and then perform a bunch of reads, and check the number of device reads.
```c 
void test_main (void) {
    a. Record the initial disk read and write counts to subtract from later device read and write counts
    b. Initialize some local variables
    c. Reset the cache, and check to see if number of hits and accesses are 0.
    d. Create and open the file
    e. Write byte-by-byte
    f. Check if the number of device writes is roughly 128
    g. Read byte-by-byte
    h. Check if the number of device reads is roughly 128
}
```
#### Output
```
Copying tests/filesys/extended/my-test-2 to scratch partition...
Copying tests/filesys/extended/tar to scratch partition...
qemu-system-i386 -device isa-debug-exit -hda /tmp/JlUrNyVIyR.dsk -hdb tmp.dsk -m 4 -net none -nographic -monitor null
PiLo hda1
Loading..............
Kernel command line: -q -f extract run my-test-2
Pintos booting with 3,968 kB RAM...
367 pages available in kernel pool.
367 pages available in user pool.
Calibrating timer...  64,716,800 loops/s.
hda: 1,008 sectors (504 kB), model "QM00001", serial "QEMU HARDDISK"
hda1: 224 sectors (112 kB), Pintos OS kernel (20)
hda2: 200 sectors (100 kB), Pintos scratch (22)
hdb: 5,040 sectors (2 MB), model "QM00002", serial "QEMU HARDDISK"
hdb1: 4,096 sectors (2 MB), Pintos file system (21)
filesys: using hdb1
scratch: using hda2
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'my-test-2' into the file system...
Putting 'tar' into the file system...
Erasing ustar archive...
Executing 'my-test-2':
(my-test-2) begin
(my-test-2) Number of initial cache accesses: 0
(my-test-2) Number of initial cache hits: 0
(my-test-2) create "logfile"
(my-test-2) open "logfile"
(my-test-2) The number of devices writes should be near 128
(my-test-2) The number of device reads should be near 128
(my-test-2) end
my-test-2: exit(0)
Execution of 'my-test-2' complete.
Timer: 1175 ticks
Thread: 59 idle ticks, 66 kernel ticks, 1051 user ticks
hdb1 (filesys): 381 reads, 378 writes
hda2 (scratch): 199 reads, 2 writes
Console: 1266 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```
#### Non-trivial bugs:
1. If for some reason the kernel tried to clear the cache but didn't actually re-initialize the cache block locks, then we wouldn't be able to finish the test and would end up getting a random error.
2. The test assumes that we write-back cache, so if we had a write-behind implementation, the kernel may output an even higher amount of device writes that what we would expect.

### Experience Writing Tests

- The only hinderance was figuring out how to use a Perl script, but other than that learning how to write tests for this project wasn't too bad.
- While writing tests are easy, knowing how to write good tests is almost an art to be learned through lots of experience, because test outputs can be either useless or very informative.
- Sometimes it's not good to overthink writing tests out.  What we mean is that we should first code out a barebones expected output, then find any edge cases and check for those to flush out tests even more, rather than getting stuck thinking about bizarre edge cases.
- We couldn't find a way to do float division because of this bug: 
```
undefined reference to `__floatsisf'
```
- We noteced that there is arithmetic.c, but we weren't sure we could use it for our tests, so while we wanted to calculate the hit rate for the first cache test, we weren't able to do so.