#include <syscall.h>
#include "tests/filesys/extended/mk-tree.h"
#include "tests/lib.h"
#include "tests/filesys/extended/syn-rw.h"
#include <stdio.h>

/* Returns true if NUMBER is within a difference of 20 from 128 */
bool is_close_to_one_twenty_eight(long long number) {
    return 108 <= number && number <= 148;
}

/* This tests my buffer cache's ability to calesce writes to the same sector. */
void test_main (void) {
    /* Immediately record the initial disk read and write counts. */
    long long i_read_cnt = number_device_reads();
    long long i_write_cnt = number_device_writes();

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

    /* Perform a bunch of writes. */
    for (int i = 0; i < buffer_size; i++) {
        char c = 'a';
        write (fd, &c, 1);
    }

    /* The number of device writes should be roughly 128 */
    long long num_dev_writes = number_device_writes() - i_write_cnt;
    CHECK(is_close_to_one_twenty_eight(num_dev_writes), "The number of devices writes should be near 128");

    /* Finally, read a bunch. */
    for (int i = 0; i < buffer_size; i++) {
        char d;
        read (fd, &d, 1);
    }

    /* Calculate the ratio of cache accesses to the number of times we read from the device. */
    CHECK(is_close_to_one_twenty_eight(num_dev_writes), "The number of device reads should be near 128");
}