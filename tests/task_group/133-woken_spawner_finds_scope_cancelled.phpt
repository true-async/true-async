--TEST--
TaskGroup: a spawner woken for a queue place that finds the external scope cancelled before it runs cancels the group and throws
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
$first_gate = new FutureState();
$group = new TaskGroup(concurrency: 1, queueLimit: 1, scope: $scope);
$group->spawn(fn() => (new Future($first_gate))->await());
$group->spawn(fn() => (new Future(new FutureState()))->await());
$spawner = spawn(function () use ($group) {
    try {
        $group->spawn(fn() => "third");
        echo "third accepted\n";
    } catch (Async\AsyncException $exception) {
        echo "spawner: ", $exception->getMessage(), "\n";
    }
});

suspend();
$first_gate->complete(null);
// The first task's end starts the queued one and wakes the spawner, which runs after the cancel.
suspend();
$scope->cancel();
await($spawner);
echo "closed: ", var_export($group->isClosed(), true), "\n";
?>
--EXPECT--
spawner: Cannot spawn tasks on a closed TaskGroup
closed: true
