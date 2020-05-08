# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::random;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(my-test-1) begin
(my-test-1) Number of initial cache accesses: 0
(my-test-1) Number of initial cache hits: 0
(my-test-1) create "logfile"
(my-test-1) open "logfile"
(my-test-1) Number of first set of cache accesses: 12395
(my-test-1) Number of first set of cache hits: 12380
(my-test-1) open "logfile"
(my-test-1) Number of second set of cache accesses: 12299
(my-test-1) Number of second set of cache hits: 12299
(my-test-1) end
EOF
pass;