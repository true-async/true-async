--TEST--
The automatic run reports a member stuck on a dead Future, which alone holds its Scope object; then another member's unheld error takes the route, which cancels the found member; the oracle accepts the wake
--INI--
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\delay;

function start(): void
{
    $scope = new Scope();
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
echo "not reached\n";
?>
--EXPECTF--
Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (await: future) in %s on line %d
stuck: Coroutine cancelled

Fatal error: Uncaught RuntimeException: boom in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d
