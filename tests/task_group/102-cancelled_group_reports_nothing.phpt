--TEST--
TaskGroup: dropping a cancelled group reports nothing, and its scope's parent goes on
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\Scope;
use Async\TaskGroup;
use function Async\await;
use function Async\suspend;

$scope = new Scope();
$scope->setExceptionHandler(function ($scope, $coroutine, $error) {
    echo "handler: ", get_class($error), "\n";
});
$coroutine = $scope->spawn(function () {
    $group = new TaskGroup();
    $group->spawn(function () {
        (new Future(new FutureState()))->await();
    });

    suspend();
    $group->cancel();
    $group->awaitCompletion();
    echo "completed\n";
    unset($group);
    echo "after unset\n";
});

await($coroutine);
await($scope->spawn(fn() => print "the scope still runs\n"));
?>
--EXPECT--
completed
after unset
the scope still runs
