# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::random;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(my-test-2) begin
(my-test-2) Number of initial cache accesses: 0
(my-test-2) Number of initial cache hits: 0
(my-test-2) create "logfile"
(my-test-2) open "logfile"
(my-test-2) The number of devices writes should be near 128
(my-test-2) The number of device reads should be near 128
(my-test-2) end
EOF
pass;

