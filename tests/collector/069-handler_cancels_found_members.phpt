--TEST--
The automatic run reports a member stuck on a dead Future, which alone holds its Scope object; then another member's error reaches a scope handler that cancels the scope: the found member is woken by the route, which the oracle accepts
--INI--
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Scope;
use Async\Coroutine;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\delay;

function start(): void
{
    $scope = new Scope();
    $scope->setExceptionHandler(function (Scope $scope, Coroutine $coroutine, Throwable $e) {
        echo "handler: ", $e->getMessage(), "\n";
        $scope->cancel();
    });
    $scope->spawn(function () use ($scope) {
        try {
            await(new Future(new FutureState()));
        } catch (Async\AsyncCancellation $e) {
            echo "stuck: ", $e->getMessage(), "\n";
        }
    });
    $scope->spawn(function () {
        delay(20);
        throw new RuntimeException("boom");
    });
}

start();
delay(50);
echo "end\n";
?>
--EXPECTF--
Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (await: future) in %s on line %d
handler: boom
stuck: Scope was cancelled
end
