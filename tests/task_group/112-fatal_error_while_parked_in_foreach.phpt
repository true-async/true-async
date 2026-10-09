--TEST--
TaskGroup: a fatal error in a task while the main code is parked in a foreach step ends the request cleanly
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

$group = new TaskGroup();
$group->spawn(function () {
    str_repeat('x', 10000000);
});

foreach ($group as $key => $pair) {
    echo "yielded $key\n";
}
?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
shutdown
