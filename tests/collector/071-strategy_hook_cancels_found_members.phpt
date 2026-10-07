--TEST--
The automatic run reports a member of a scope whose object only a stuck coroutine holds; a live member then spawns through a SpawnStrategy, whose hook gets the scope's object and cancels it: the collector leaves that path out, and the oracle accepts the wake
--INI--
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Scope;
use Async\Coroutine;
use Async\SpawnStrategy;
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\spawn_with;
use function Async\await;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

final class CancellingStrategy implements SpawnStrategy
{
    public function provideScope(): ?Scope
    {
        return null;
    }

    public function beforeCoroutineEnqueue(Coroutine $coroutine, Scope $scope): array
    {
        $scope->cancel();
        return [];
    }

    public function afterCoroutineEnqueue(Coroutine $coroutine, Scope $scope): void
    {
    }
}

function start(): void
{
    $scope = new Scope();
    $scope->spawn(function () {
        try {
            await(new Future(new FutureState()));
        } catch (Async\AsyncCancellation $e) {
            echo "member: ", $e->getMessage(), "\n";
        }
    });
    $scope->spawn(function () {
        delay(20);
        spawn_with(new CancellingStrategy(), function () {
        });
    });
    spawn(function () use ($scope) {
        await(new Future(new FutureState()));
    });
}

start();
delay(50);
foreach (get_deadlocked_coroutines() as $coroutine) {
    $coroutine->cancel();
}
delay(10);
echo "end\n";
?>
--EXPECTF--
Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (await: future) in %s on line %d

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (await: future) in %s on line %d
member: Scope was cancelled
end
