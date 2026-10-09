--TEST--
TaskGroup: a task end wakes a foreach parked behind an awaitCompletion() that waits for the completion
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\await;
use function Async\spawn;

$last = new FutureState();
$first = new FutureState();
$group = new TaskGroup();
$group->spawn(fn() => (new Future($last))->await());
$group->spawn(fn() => (new Future($first))->await());
$group->close();

$waiter = spawn(function () use ($group) {
    $group->awaitCompletion();
});
$read = false;
$reader = spawn(function () use ($group, &$read) {
    foreach ($group as $key => [$result, $error]) {
        echo "read $key: $result\n";
        $read = true;
    }
});

while (!$waiter->isStarted() || !$reader->isStarted()) {
    Async\suspend();
}
$first->complete("first");
for ($i = 0; $i < 10 && !$read; $i++) {
    Async\suspend();
}
echo "last opens\n";
$last->complete("last");
await($reader);
await($waiter);
echo "completed\n";
?>
--EXPECT--
read 1: first
last opens
read 0: last
completed
