--TEST--
TaskGroup: the closing interrupts a zombie of its external Scope, which a safe cancel left running
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\Scope;
use Async\TaskGroup;

$started = new FutureState();
$ended = new FutureState();
$scope = Scope::inherit();
$zombie = $scope->spawn(function () use ($started, $ended) {
    $started->complete(null);

    try {
        (new Future(new FutureState()))->await();
    } catch (Async\AsyncCancellation $error) {
        echo "zombie: ", $error->getMessage(), "\n";
        $ended->complete(null);
    }
});

(new Future($started))->await();
$scope->cancel();
Async\suspend();
var_dump($zombie->isCompleted());

$group = new TaskGroup(scope: $scope);
unset($group, $scope);
(new Future($ended))->await();
?>
--EXPECT--
bool(false)
zombie: Scope is being disposed due to TaskGroup destruction
