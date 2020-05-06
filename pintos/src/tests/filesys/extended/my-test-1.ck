# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::random;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(my-test-1) begin
(my-test-1) create "logfile"
(my-test-1) open "logfile"
(my-test-1) Number of bytes read: 4096
(my-test-1) Number of bytes read: 8

EOF
pass;