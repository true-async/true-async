--TEST--
TaskGroup: a fatal error with a task running and one queued ends the request cleanly
--INI--
memory_limit=2M
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

register_shutdown_function(function () {
    echo "shutdown\n";
});

$started = new FutureState();
$group = new TaskGroup(concurrency: 1);
$group->spawn(function () use ($started) {
    $started->complete(null);
    (new Future(new FutureState()))->await();
});
$group->spawn(function () {
    echo "queued task ran\n";
});

(new Future($started))->await();
str_repeat('x', 10000000);
?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
shutdown
