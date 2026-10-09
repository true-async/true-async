--TEST--
TaskGroup: spawn() after its external Scope was freed throws the closed scope's message
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\Scope;
use Async\TaskGroup;

$scope = new Scope();
$outsider = $scope->spawn(fn() => (new Future(new FutureState()))->await());
$group = new TaskGroup(scope: $scope);

$scope->cancel();

try {
    Async\await($outsider);
} catch (Async\AsyncCancellation $error) {
    echo "outsider cancelled\n";
}

try {
    $group->spawn(function () {
        echo "task ran\n";
    });
} catch (Async\AsyncException $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
outsider cancelled
Cannot spawn a coroutine in a closed scope
