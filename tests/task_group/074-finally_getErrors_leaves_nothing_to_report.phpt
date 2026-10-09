--TEST--
TaskGroup: a finally handler that reads getErrors() leaves no error to report
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
    $failed = new FutureState();
    $group = new TaskGroup();
    $group->spawn(function () use ($failed) {
        $failed->complete(null);
        throw new RuntimeException("boom");
    });
    $group->finally(function (TaskGroup $group) {
        echo "finally: ", $group->getErrors()[0]->getMessage(), "\n";
    });
    (new Future($failed))->await();
    $group->close();
});

$scope->awaitCompletion(new Future(new FutureState()));
echo "end\n";
?>
--EXPECT--
finally: boom
end
