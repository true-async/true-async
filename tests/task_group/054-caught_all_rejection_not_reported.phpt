--TEST--
TaskGroup: an error received through a caught all() rejection is not reported when the group is dropped
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\Scope;
use Async\TaskGroup;

$scope = new Scope();
$scope->setExceptionHandler(function ($scope, $coroutine, $error) {
    echo "handler: ", get_class($error), "\n";
});

$scope->spawn(function () {
    $group = new TaskGroup();
    $group->spawn(function () {
        throw new RuntimeException("boom");
    });
    $group->close();

    try {
        $group->all()->await();
    } catch (Async\CompositeException $error) {
        echo "caught: ", $error->getExceptions()[0]->getMessage(), "\n";
    }

    unset($group);
    echo "after unset\n";
});

$scope->awaitCompletion(new Future(new FutureState()));
echo "end\n";
?>
--EXPECT--
caught: boom
after unset
end
