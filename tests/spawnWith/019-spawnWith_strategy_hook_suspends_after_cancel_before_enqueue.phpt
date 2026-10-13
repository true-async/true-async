--TEST--
Async\spawn_with: a SpawnStrategy hook that cancels the coroutine and suspends before it is queued
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;
use Async\SpawnStrategy;
use function Async\spawn_with;
use function Async\suspend;

class SuspendingStrategy implements SpawnStrategy
{
    public function provideScope(): ?Scope
    {
        return null;
    }

    public function beforeCoroutineEnqueue(Coroutine $coroutine, Scope $scope): array
    {
        $coroutine->cancel();
        suspend();

        return [];
    }

    public function afterCoroutineEnqueue(Coroutine $coroutine, Scope $scope): void
    {
    }
}

// The coroutine is cancelled and finishes unrun before it would be queued.
$coroutine = spawn_with(new SuspendingStrategy(), fn() => print("must not run\n"));
var_dump($coroutine->isCompleted(), $coroutine->isCancelled());

echo "end\n";

?>
--EXPECT--
bool(true)
bool(true)
end
