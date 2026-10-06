--TEST--
max_execution_time ends a request whose coroutines all wait where the timer counts wall time (ZTS on Linux and FreeBSD, Windows)
--SKIPIF--
<?php
/* Elsewhere the timer is ITIMER_PROF, which counts CPU time: an idle wait never reaches it. */
if (PHP_OS_FAMILY !== 'Windows' && !(PHP_ZTS && in_array(PHP_OS_FAMILY, ['Linux', 'BSD'], true))) {
    die("skip max_execution_time counts CPU time here");
}
?>
--FILE--
<?php
set_time_limit(1);
TrueAsync\Test\reactor_wait(3000);
echo "not reached\n";
?>
--EXPECTF--
Fatal error: Maximum execution time of 1 second exceeded in %s on line %d
