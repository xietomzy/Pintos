/* Open a file. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  reset_cache();
  number_cache_accesses();
  int handle = open ("sample.txt");
  if (handle < 2)
    fail ("open() returned %d", handle);
  // reset_cache();
}
