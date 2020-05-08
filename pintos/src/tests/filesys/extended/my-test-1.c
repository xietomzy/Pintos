#include <syscall.h>
#include "tests/filesys/extended/mk-tree.h"
#include "tests/lib.h"
#include "tests/filesys/extended/syn-rw.h"
#include <stdio.h>

void test_main(void);

static char buf[BUF_SIZE];


void test_main (void) {
    /* Some variables: fd, number of accesses and hits. */
    int fd;
    int initial_number_of_hits;
    int initial_number_of_accesses;
    int first_number_of_accesses;
    int second_number_of_accesses;
    int first_number_of_hits;
    int second_number_of_hits;

    /* Reset the cache, and check to see if number of hits and accesses are 0. */
    reset_cache();
    initial_number_of_accesses = number_cache_accesses();
    initial_number_of_hits = number_cache_hits();
    msg("Number of initial cache accesses: %i", initial_number_of_accesses);
    msg("Number of initial cache hits: %i", initial_number_of_hits);        

    /* Create and open the file */
    CHECK (create (file_name, BUF_SIZE), "create \"%s\"", file_name);
    CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);

    /* Perform a bunch of reads. */
    for (size_t i = 0; i < sizeof buf; i++) {
        char c;
        read (fd, &c, 1);
    }

    /* Calculate the number of cache hits. */
    first_number_of_hits = number_cache_hits();
    first_number_of_accesses = number_cache_accesses(); 
    msg("Number of first set of cache accesses: %i", first_number_of_accesses);
    msg("Number of first set of cache hits: %i", first_number_of_hits);

    /* Close and reopen the file. */
    close(fd);
    CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);

    /* Perform a bunch of reads again. */
    for (size_t i = 0; i < sizeof buf; i++) {
        char c;
        read (fd, &c, 1);
    }    

    /* Find the number of cache hits this time. */
    second_number_of_hits = number_cache_hits() - first_number_of_hits;
    second_number_of_accesses = number_cache_accesses() - first_number_of_accesses;
    msg("Number of second set of cache accesses: %i", second_number_of_accesses);
    msg("Number of second set of cache hits: %i", second_number_of_hits);
}