--TEST--
TaskGroup: a successful race() covers the failure of a task still running then, so it is not reported
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
    $gate = new FutureState();
    $failing = new FutureState();
    $group = new TaskGroup();
    $group->spawn(fn() => "first");
    $group->spawn(function () use ($gate, $failing) {
        (new Future($gate))->await();
        $failing->complete(null);
        throw new RuntimeException("late");
    });

    echo "race: ", $group->race()->await(), "\n";

    $gate->complete(null);
    (new Future($failing))->await();
    var_dump($group->isFinished());
    unset($group);
    echo "after unset\n";
});

$scope->awaitCompletion(new Future(new FutureState()));
echo "end\n";
?>
--EXPECT--
race: first
bool(true)
after unset
end
