--TEST--
TaskGroup: a task added after a successful race() is not covered by it, so its failure is reported
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\Scope;
use Async\TaskGroup;

$reported = new FutureState();
$scope = new Scope();
$scope->setExceptionHandler(function ($scope, $coroutine, $error) use ($reported) {
    echo "handler: ", get_class($error), ": ", $error->getExceptions()[0]->getMessage(), "\n";
    $reported->complete(null);
});

$scope->spawn(function () {
    $failing = new FutureState();
    $group = new TaskGroup();
    $group->spawn(fn() => "first");
    echo "race: ", $group->race()->await(), "\n";

    $group->spawn(function () use ($failing) {
        $failing->complete(null);
        throw new RuntimeException("added later");
    });
    (new Future($failing))->await();
    unset($group);
});

(new Future($reported))->await();
echo "end\n";
?>
--EXPECT--
race: first
handler: Async\CompositeException: added later
end
