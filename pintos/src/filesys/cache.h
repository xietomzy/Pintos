#include "../devices/block.h"
#include "../threads/synch.h"
#include "off_t.h"

/* Public api for the cache.  
 * This was suggested to us by Neil; that we
 * Should create a different file for the cache rather 
 * than putting it in inode.c. */

/* According to the spec, is the maximum amount of cache blocks. */
#define MAX_CACHE_BLOCKS 64

/* A lock that any thread must acquire in order to access the cache. */
struct lock cache_lock;

/* The block for our buffer cache for our file system. */
struct cache_block {
    struct list_elem elem; // This is actually part of the cache, which is a list of cache blocks
    block_sector_t sector; // The sector of this block.
    struct lock cache_block_lock; // Cache block operations need to be serialized
    bool valid; // valid bit
    bool dirty; // dirty bit
    bool clock_bit; // bit that determines which group it's in for the clock algorithm (young/old)
    uint8_t *data; // pointer to data
};

/* Our cache is represented as a list of cache blocks.
 * We structure the cache so that the most recently used element
 * is at the end of the list, while the most recently used element
 * is at the front of the list. */
struct list cache;

void cache_init (void);
void cache_flush (void);
void cache_read (struct block *block, block_sector_t sector, void *buffer);
void cache_write (struct block *block, block_sector_t sector, const void *buffer);

