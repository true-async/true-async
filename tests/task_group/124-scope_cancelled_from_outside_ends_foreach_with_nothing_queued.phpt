--TEST--
TaskGroup: a scope cancelled from outside seals the group at its task's end with nothing queued, so a foreach ends
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\Scope;
use Async\TaskGroup;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$scope = new Scope();
$group = new TaskGroup(scope: $scope);
$group->spawn(fn() => (new Future(new FutureState()))->await());

$consumer = spawn(function () use ($group) {
    foreach ($group as $key => [$result, $error]) {
        echo "$key: ", get_class($error), "\n";
    }

    echo "foreach ended, closed: ", var_export($group->isClosed(), true), "\n";
});

// The task and the consumer park.
suspend();
$scope->cancel();
await($consumer);
?>
--EXPECT--
0: Async\AsyncCancellation
foreach ended, closed: true
