--TEST--
TaskGroup: a dropped group reports its unseen errors after its finally handlers ran
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\Scope;
use Async\TaskGroup;

$scope = new Scope();
$scope->setExceptionHandler(function ($scope, $coroutine, $error) {
    echo "handler: ", get_class($error), ": ", $error->getExceptions()[0]->getMessage(), "\n";
});

$scope->spawn(function () {
    $failed = new FutureState();
    $group = new TaskGroup();
    $group->spawn(function () use ($failed) {
        $failed->complete(null);
        throw new RuntimeException("boom");
    });
    $group->finally(function () {
        echo "finally\n";
    });

    (new Future($failed))->await();
    unset($group);
    echo "after unset\n";
});

$scope->awaitCompletion(new Future(new FutureState()));
echo "end\n";
?>
--EXPECT--
after unset
finally
handler: Async\CompositeException: boom
end
