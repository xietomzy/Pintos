#include <syscall.h>
#include "tests/filesys/extended/mk-tree.h"
#include "tests/lib.h"
#include "tests/filesys/extended/syn-rw.h"

static char buf[BUF_SIZE];


void test_main (void) {
    int fd;
    int first_number_of_accesses;
    int second_number_of_accesses;

    int first_number_of_hits;
    int second_number_of_hits;

    CHECK (create (file_name, BUF_SIZE), "create \"%s\"", file_name);
    CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
    for (int i = 0; i < sizeof buf; i++) 
    {
        char c;
        // CHECK (read (fd, &c, 1) > 0, "read \"%s\"", file_name);
        read (fd, &c, 1) > 0;
    }
    CHECK(number_cache_accesses() == 4096,"Number of bytes read: %i", 4096);
    CHECK(number_cache_hits(),"Number of cache hits: %i", number_cache_hits());

    close()

    // msg ("Number of bytes read: %i", read(fd, buf1, sizeof buf1));

}