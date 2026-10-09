--TEST--
TaskGroup: a fatal error in a task while the main code is parked in spawn() on a full queue ends the request cleanly
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

use Async\TaskGroup;

register_shutdown_function(function () {
    echo "shutdown\n";
});

$group = new TaskGroup(concurrency: 1, queueLimit: 1);
$group->spawn(function () {
    str_repeat('x', 10000000);
});
$group->spawn(function () {
    echo "queued task ran\n";
});
$group->spawn(function () {
    echo "third task ran\n";
});
echo "accepted\n";
?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
shutdown
