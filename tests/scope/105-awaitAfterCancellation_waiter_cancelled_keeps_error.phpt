--TEST--
Scope: a waiter in awaitAfterCancellation() cancelled after a member's error reached it, before it ran, throws the cancellation with that error as its previous
--DESCRIPTION--
Both zombies wait on one Future, whose completion queues them in order; the first one's error queues the
waiter behind the second, which cancels it.
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await;
use function Async\delay;

$state = new FutureState();
$future = new Future($state);
$waiter = null;

$scope = Scope::inherit()->allowZombies();
$scope->spawn(function () use ($future) {
    await($future);
    throw new RuntimeException("member error");
});
$scope->spawn(function () use ($future, &$waiter) {
    await($future);
    $waiter->cancel();
});
Async\suspend();

$waiter = spawn(function () use ($scope) {
    $scope->cancel();
    $scope->awaitAfterCancellation(function (Throwable $e) {
        echo "handler: ", $e->getMessage(), "\n";
    });
    echo "waiter done\n";
});
Async\suspend();
$state->complete(1);

try {
    await($waiter);
} catch (Async\AsyncCancellation $e) {
    echo get_class($e), ", previous: ", $e->getPrevious()?->getMessage(), "\n";
}
echo "end\n";
?>
--EXPECT--
Async\AsyncCancellation, previous: member error
end
