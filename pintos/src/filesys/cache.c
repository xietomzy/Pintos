#include "off_t.h"
#include "../devices/block.h"
#include "cache.h"
#include "../threads/synch.h"
#include "list.h"

/* Initializes the cache. */
void cache_init (void) {
    list_init(&cache);
}

/* Assumes caller is cache_read.  Checks to see if the block's sector has 
 * remained the same, because as soon as the thread releases the cache lock,
 * there remains a possibility that another thread could evict the block
 * with sector SECTOR, meaning that we have to iterate through cache_read again.
 * This returns true if the block's sector hasn't changed from the time our caller.
 * cache_read, started executing. */
bool block_has_not_mutated (struct cache_block *block, block_sector_t sector) {
    return block->sector != sector;
}

/* Returns true if we have space in our cache (less than or equal to 64 blocks) */
bool cache_has_space () {
    return list_size(&cache) <= MAX_CACHE_BLOCKS;
}

/* Assumes the caller is inode_read_at (I think?)
 * 
 * The algorithm just iterates through the cache and checks to see if a 
 * block with sector SECTOR resides in there.
 * 
 * Here is what is written from inode_read_at:
 * 
 * Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 * Returns the number of bytes actually read, which may be less
 * than SIZE if an error occurs or end of file is reached. */
void cache_read (struct block *block, block_sector_t sector, void *buffer) {

    /* We must acquire a lock to start reading the cache. Should we do that before calling
     * this function? */
    lock_acquire(&cache_lock);

    /* Perform basic list iteration. */

    struct list_elem *cache_blk_e;
    for (cache_blk_e = list_begin(&cache); cache_blk_e != list_end(&cache);
         cache_blk_e = list_next(cache_blk_e)) {

        struct cache_block *cache_blk = list_entry(cache_blk_e, struct cache_block, elem);
        if (cache_blk->sector == sector) {

            /* Before accessing the block, we must have a lock, and make sure to release the lock. */

            /* Neil: "block may have changed in the meantime, 
             * so check if the block is correct after acquiring the block lock" */

            lock_acquire(&(cache_blk->cache_block_lock));

            lock_release(&cache_lock);

            if (block_has_mutated(cache_blk, sector)) {
                lock_release(&(cache_blk->cache_block_lock));
                cache_read(block, sector, buffer);
            } else {
                block_read(block, sector, buffer);
                lock_release(&(cache_blk->cache_block_lock));
            }

            return;
        }
    }

    /* At this point, we still have the cache lock. We must insert into our cache since we couldn't find
    our sector in our cache. */
    if (cache_has_space()) {
        struct cache_block blk_to_add;
        blk_to_add->sector = sector;
        lock_init(&(blk_to_add->cache_block_lock));
        blk_to_add->valid = true;
        blk_to_add->dirty = false;
    }
    

    lock_release(&cache_lock);
    // Should I put block_read here?
    block_read(block, sector, buffer);
}

/* By using the cache, we write sector SECTOR to BLOCK from BUFFER, which must contain
   BLOCK_SECTOR_SIZE bytes.  Returns after the block device has
   acknowledged receiving the data.
   Internally synchronizes accesses to block devices, so external
   per-block device locking is unneeded. */
void cache_write (struct block *block, block_sector_t sector, const void *buffer) {
    return;
}
