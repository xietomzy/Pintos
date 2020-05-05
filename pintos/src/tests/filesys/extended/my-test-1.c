#include <stdio.h>
#include <syscall.h>
#include "tests/filesys/extended/mk-tree.h"
#include "tests/lib.h"
#include "tests/filesys/extended/syn-rw.h"


void test_main (void) {
    char buf1[BUF_SIZE];
    int fd;
    int first_number_of_accesses;
    int second_number_of_accesses;

    int first_number_of_hits;
    int second_number_of_hits;

    CHECK (create (file_name, BUF_SIZE), "create \"%s\"", file_name);
    CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);

    msg ("Number of bytes read: %i", read(fd, buf1, sizeof buf1));

}