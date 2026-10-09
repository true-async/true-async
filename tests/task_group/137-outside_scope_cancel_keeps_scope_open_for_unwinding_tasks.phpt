--TEST--
TaskGroup: a task end after the external scope's cancel seals the group without closing the scope, so another task's catch can still spawn into it
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\Scope;
use Async\TaskGroup;
use function Async\suspend;

$scope = new Scope();
$scope->finally(function () {
    echo "scope finally\n";
});
$group = new TaskGroup(scope: $scope);
$group->spawn(fn() => (new Future(new FutureState()))->await());
$group->spawn(function () use ($scope) {
    try {
        (new Future(new FutureState()))->await();
    } catch (Async\AsyncCancellation) {
        $scope->spawn(function () {
            echo "cleanup\n";
        });
    }
});

// Both tasks park.
suspend();
$scope->cancel();
suspend();
echo "closed: ", var_export($group->isClosed(), true), "\n";
?>
--EXPECT--
closed: true
cleanup
scope finally
