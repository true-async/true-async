--TEST--
TaskGroup: when its external Scope is cancelled from outside, a queued task never starts and ends cancelled
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\Scope;
use Async\TaskGroup;

$started = new FutureState();
$scope = new Scope();
$group = new TaskGroup(concurrency: 1, scope: $scope);
$group->spawn(function () use ($started) {
    $started->complete(null);
    (new Future(new FutureState()))->await();
});
$group->spawn(function () {
    echo "queued task ran\n";
});

(new Future($started))->await();
$scope->cancel(new Async\AsyncCancellation("outside"));
$group->all(ignoreErrors: true)->await();

foreach ($group->getErrors() as $key => $error) {
    echo $key, ": ", get_class($error), ": ", $error->getMessage(), "\n";
}

var_dump($group->isClosed());
?>
--EXPECT--
0: Async\AsyncCancellation: outside
1: Async\AsyncCancellation: TaskGroup cancelled
bool(true)
