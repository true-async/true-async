--TEST--
TaskGroup: a fatal error while the reporter of a dropped group is queued releases its report
--INI--
memory_limit=2M
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->spawn(function () {
    throw new RuntimeException("nobody read me");
});
$group->close();
$group->awaitCompletion();
unset($group);

register_shutdown_function(function () {
    echo "shutdown\n";
});

str_repeat('x', 10000000);
?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
shutdown
