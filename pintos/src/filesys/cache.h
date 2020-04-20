#include "../devices/block.h"
#include "../threads/synch.h"

/* Public api for the cache.  
 *  This was suggested to us by Neil; that we
 *  should create a different file for the cache rather 
 *  than putting it in inode.c. */

/* According to the spec, is the maximum amount of cache blocks. */
#define MAX_CACHE_BLOCKS 64

/* The block for our buffer cache for our file system. */
struct cache_block {
    block_sector_t sector; // The sector of this block.
    struct lock cache_block_lock;        // Cache block operations need to be serialized
    bool valid;                    // valid bit
    bool dirty;                    // dirty bit
    bool clock_bit;                // bit that determines which group it's in for the clock algorithm (young/old)
    uint8_t *data;                    // pointer to data
};

struct cache_block cache[MAX_CACHE_BLOCKS];

void cache_flush(void);