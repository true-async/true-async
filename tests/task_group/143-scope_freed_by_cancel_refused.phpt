--TEST--
TaskGroup: a Scope whose cancel freed it once its coroutine ended is refused
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\Scope;
use Async\TaskGroup;
use function Async\await;
use function Async\suspend;

$scope = new Scope();
$coroutine = $scope->spawn(fn() => (new Future(new FutureState()))->await());
suspend();
$scope->cancel();

try {
    await($coroutine);
} catch (Async\AsyncCancellation $e) {
    echo "member cancelled\n";
}

try {
    new TaskGroup(scope: $scope);
} catch (Async\AsyncException $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
member cancelled
Cannot use a disposed Scope for TaskGroup
