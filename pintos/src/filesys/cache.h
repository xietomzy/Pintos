#include "devices/block.h"
#include "threads/synch.h"
#include "filesys/off_t.h"
#include <list.h>
#include <stdbool.h>
/* Public API for the cache. */

#define MAX_CACHE_BLOCKS 64

/* List showing lru order of cache*/
struct list lru;

/* A lock that any thread must acquire in order to access the cache. */
struct lock cache_lock;


/* The block for our buffer cache for our file system. */
struct cache_block {
    struct list_elem elem; // Cache block list elem
    block_sector_t sector; // The sector of this block
    struct lock cache_block_lock; // Cache block operations need to be serialized
    bool valid; // valid bit
    bool dirty; // dirty bit
    uint8_t data[BLOCK_SECTOR_SIZE]; // data
};

/* Cache is an array of cache blocks */
struct cache_block cache[MAX_CACHE_BLOCKS];

void cache_init (void);
/* Cache read/write similar to block read/write */
void cache_read (struct block *block, block_sector_t sector, void *buffer, off_t offset, int chunk_size);
void cache_write (struct block *block, block_sector_t sector, void *buffer, off_t offset, int chunk_size);
void cache_flush (void);
int num_cache_hits(void);
int num_cache_accesses(void);