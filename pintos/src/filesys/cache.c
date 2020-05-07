#include "filesys/cache.h"
#include "filesys/filesys.h"
#include <string.h>

struct list lru;
struct lock cache_lock;
struct cache_block cache[MAX_CACHE_BLOCKS];

struct cache_block *cache_get_block(block_sector_t sector); 
void lru_move_front(struct cache_block *block);
void new_block_read(struct block *device, struct cache_block *block, block_sector_t sector, void *buffer, off_t offset, size_t chunk_size);
void new_block_write(struct block *device, struct cache_block *block, block_sector_t sector, const void *buffer, off_t offset, size_t chunk_size);
/* Initialize the cache. */
void cache_init (void) {
    list_init(&lru);
    lock_init(&cache_lock);
    for (int i = 0; i < MAX_CACHE_BLOCKS; i++) {
        lock_init(&(cache[i].cache_block_lock));
    }
}
/* Tries to get block in cache, returns NULL if not in cache
    Global lock must be held before calling this function
 */
struct cache_block *cache_get_block(block_sector_t sector) {
    struct list_elem *cache_blk_e;
    if (list_empty(&lru)) {
        return NULL;
    }
    for (cache_blk_e = list_begin(&lru); cache_blk_e != list_end(&lru); cache_blk_e = list_next(cache_blk_e)) {
        struct cache_block *cache_blk = list_entry(cache_blk_e, struct cache_block, elem);
        if (cache_blk->sector == sector) {
            return cache_blk;
        }
    }
    return NULL;
}

/* Pushes most recently used block to front of lru list 
 The block is in the list already, so remove it from the list first 
 This function is only used if the block is in the cache
*/
void lru_move_front(struct cache_block *block) {
    lock_acquire(&cache_lock);
    list_remove(&(block->elem));
    list_push_front(&lru, &(block->elem));
    lock_release(&cache_lock);
}
/* Performs new block operations when block is pulled into the cache
*/

void new_block_read(struct block *device, struct cache_block *block, block_sector_t sector, void *buffer, off_t offset, size_t chunk_size) {
    block_read(device, sector, block->data);
    block->valid = true;
    block->sector = sector;
    memcpy(buffer, block->data + offset, chunk_size);
}

void new_block_write(struct block *device, struct cache_block *block, block_sector_t sector, const void *buffer, off_t offset, size_t chunk_size) {
    block_read(device, sector, block->data);
    block->valid = true;
    block->sector = sector;
    memcpy(block->data + offset, buffer, chunk_size);
    block->dirty = true;
}

void cache_read (struct block *block, block_sector_t sector, void *buffer, off_t offset, int chunk_size) {

    /* We must acquire a lock to start reading the cache. */
    lock_acquire(&cache_lock);
    /* Check if block is in cache */
    struct cache_block *cache_blk = cache_get_block(sector);
    /* If in the cache */
    if ((cache_blk != NULL) && (cache_blk->sector == sector)) {
        lock_release(&cache_lock);
        /* thread can block between here, which means the cache_blk can change */
        lock_acquire(&(cache_blk->cache_block_lock));

        if (cache_blk->sector != sector) { // case where block has changed 
            struct cache_block *cache_blk = cache_get_block(sector);
            if (cache_blk == NULL) {
                goto cache_miss;
            }
        }
        /* Read into buffer */
        memcpy(buffer, cache_blk->data + offset, chunk_size);
        lock_release(&(cache_blk->cache_block_lock));
        lru_move_front(cache_blk);
        return;
    }
    /* Could not find the block, so we need to read it into the cache */
    cache_miss: 
    /* Load block into empty cache blocks, if any */
    for (int i = 0; i < MAX_CACHE_BLOCKS; i++) {
        if (!(cache[i].valid)) {
            lock_acquire(&(cache[i].cache_block_lock));

            new_block_read(block, &cache[i], sector, buffer, offset, chunk_size);
            list_push_front(&lru, &(cache[i].elem));
    
            lock_release(&(cache[i].cache_block_lock));
            lock_release(&cache_lock);
            return;
        }
    } 
    /* Evict a cache block to load in the new block */
    struct cache_block *evicted_blk = list_entry(list_pop_back(&lru), struct cache_block, elem);
    lock_acquire(&(evicted_blk->cache_block_lock));

    /* Need to write block to memory if block is dirty */ 
    if (evicted_blk->dirty) {
        block_write(block, evicted_blk->sector, evicted_blk->data);
    }
    /* Read in new block */
    new_block_read(block, evicted_blk, sector, buffer, offset, chunk_size);
    list_push_front(&lru, &(evicted_blk->elem));

    lock_release(&(evicted_blk->cache_block_lock));
    lock_release(&cache_lock);
}

void cache_write (struct block *block, block_sector_t sector, const void *buffer, off_t offset, int chunk_size) {
    
    /* We must acquire a lock to start reading the cache. */
    lock_acquire(&cache_lock);
    /* Check if block is in cache */
    struct cache_block *cache_blk = cache_get_block(sector);
    /* If in the cache */
    if ((cache_blk != NULL) && (cache_blk->sector == sector)) {
        lock_release(&cache_lock);
        /* thread can block between here, which means the cache_blk can change */
        lock_acquire(&(cache_blk->cache_block_lock));

        if (cache_blk->sector != sector) { // case where block has changed
            struct cache_block *cache_blk = cache_get_block(sector);
            if (cache_blk == NULL) {
                goto cache_miss;
            }
        }
        memcpy(cache_blk->data + offset, buffer, chunk_size);
        cache_blk->dirty = true;
        lock_release(&(cache_blk->cache_block_lock));
        lru_move_front(cache_blk);
        return;
    }
    /* Could not find the block, so we need to read it into the cache */
    cache_miss: 
    /* Load block into empty cache blocks, if any */
    for (int i = 0; i < MAX_CACHE_BLOCKS; i++) {
        if (!(cache[i].valid)) {
            lock_acquire(&(cache[i].cache_block_lock));

            new_block_write(block, &cache[i], sector, buffer, offset, chunk_size);
            list_push_front(&lru, &(cache[i].elem));

            lock_release(&(cache[i].cache_block_lock));
            lock_release(&cache_lock);
            return;
        }
    } 
    /* Evict a cache block to load in the new block */
    struct cache_block *evicted_blk = list_entry(list_pop_back(&lru), struct cache_block, elem);
    lock_acquire(&(evicted_blk->cache_block_lock));
    /* evict block by replacing block fields */

    if (evicted_blk->dirty) {
        block_write(block, evicted_blk->sector, evicted_blk->data);
    }

    new_block_write(block, evicted_blk, sector, buffer, offset, chunk_size);
    list_push_front(&lru, &(evicted_blk->elem));

    lock_release(&(evicted_blk->cache_block_lock));
    lock_release(&cache_lock);
}

void cache_flush (void) {
    // lock_acquire(&number_of_cache_accesses_lock);
    // number_of_cache_accesses = 0;
    // lock_release(&number_of_cache_accesses_lock);

    // lock_acquire(&number_of_hits_lock);
    // number_of_hits = 0;
    // lock_release(&number_of_hits_lock);


    /* TODO: block_write all blocks in cache to disk */
    lock_acquire(&cache_lock);
    for (int i = 0; i < MAX_CACHE_BLOCKS; i++) {
        if (cache[i].dirty) {
            lock_acquire(&(cache[i].cache_block_lock));
            block_write(fs_device, cache[i].sector, cache[i].data);
            lock_release(&(cache[i].cache_block_lock));
        }
    }

    /* Empty the LRU list. */
    while (!list_empty(&lru)) {
        list_pop_front(&lru);
    }
    memset(cache, 0, MAX_CACHE_BLOCKS * sizeof(struct cache_block));

    for (int i = 0; i < MAX_CACHE_BLOCKS; i++) {
        lock_init(&(cache[i].cache_block_lock));
    }
    lock_release(&cache_lock);
}