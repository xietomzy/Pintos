#include <syscall.h>
#include "tests/filesys/extended/mk-tree.h"
#include "tests/lib.h"
#include "tests/filesys/extended/syn-rw.h"
#include <stdio.h>

/* This tests my buffer cache's ability to calesce writes to the same sector. */
void test_main (void) {
    /* Some variables: fd, number of accesses and hits. */
    int fd;
    int buffer_size = 65536;
    int initial_number_of_accesses;
    int initial_number_of_hits;

    /* Reset the cache, and check to see if number of hits and accesses are 0. */
    reset_cache();
    initial_number_of_accesses = number_cache_accesses();
    initial_number_of_hits = number_cache_hits();
    msg("Number of initial cache accesses: %i", initial_number_of_accesses);
    msg("Number of initial cache hits: %i", initial_number_of_hits);        

    /* Create and open the file */
    CHECK (create (file_name, buffer_size), "create \"%s\"", file_name);
    CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);

    /* Perform a bunch of reads. */
    for (int i = 0; i < buffer_size; i++) {
        char c;
        read (fd, &c, 1);
    }

    /* Then see how many times we read. */
    msg("Number of cache accesses: %i", number_cache_accesses());
}