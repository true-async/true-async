--TEST--
A bailout in a coroutine while main is parked in a yield is re-raised on main's stack (S3.md 4.2, U4)
--SKIPIF--
<?php
if (getenv("USE_ZEND_ALLOC") === "0") {
    die("skip Zend MM disabled");
}
?>
--INI--
memory_limit=2M
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;

register_shutdown_function(function () {
    echo "shutdown\n";
});

spawn(function () {
    echo "coroutine\n";
    str_repeat('x', 10000000);
});

echo "main before\n";
suspend();
echo "main after (not reached)\n";
?>
--EXPECTF--
main before
coroutine

Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
shutdown
