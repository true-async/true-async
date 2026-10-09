--TEST--
TaskGroup: with no queueLimit a spawner parks once twice the concurrency of tasks wait in the queue
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\await;
use function Async\spawn;

$gate = new FutureState();
$group = new TaskGroup(concurrency: 2);

$spawner = spawn(function () use ($group, $gate) {
    $group->spawn(fn() => (new Future($gate))->await());
    $group->spawn(fn() => (new Future($gate))->await());
    for ($i = 1; $i <= 5; $i++) {
        $group->spawn(fn() => $i);
        echo "spawned $i\n";
    }
});

while (!$spawner->isStarted()) {
    Async\suspend();
}
Async\suspend();
echo "gate opens\n";
$gate->complete(0);
Async\await($spawner);
$group->close();
$group->awaitCompletion();
?>
--EXPECT--
spawned 1
spawned 2
spawned 3
spawned 4
gate opens
spawned 5
