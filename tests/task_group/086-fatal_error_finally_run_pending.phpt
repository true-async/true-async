--TEST--
TaskGroup: a fatal error while the finally run of a dropped group waits to start drops the handlers unrun
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
$group->finally(function () {
    echo "not reached\n";
});
unset($group);

str_repeat('x', 10000000);
?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
shutdown
