--TEST--
TaskGroup: awaitCompletion() of a completed group returns in scheduler context, where it would not wait
--FILE--
<?php

use Async\TaskGroup;
use TrueAsync\Test;

$group = new TaskGroup();
$group->spawn(fn() => "done");
$group->close();
$group->awaitCompletion();

Test\defer('A', null, function () use ($group) {
    $group->awaitCompletion();
    echo "returned\n";
});

Async\suspend();
?>
--EXPECT--
microtask A sched=1
returned
released A
