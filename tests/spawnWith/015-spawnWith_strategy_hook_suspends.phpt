--TEST--
Async\spawn_with: a SpawnStrategy hook that suspends while the coroutine finishes
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;
use Async\SpawnStrategy;
use function Async\await;
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
        return [];
    }

    public function afterCoroutineEnqueue(Coroutine $coroutine, Scope $scope): void
    {
        suspend();
    }
}

// The coroutine runs and finishes while the hook waits; spawn_with still returns it.
$coroutine = spawn_with(new SuspendingStrategy(), fn() => 42);
var_dump($coroutine->isCompleted(), await($coroutine));

echo "end\n";

?>
--EXPECT--
bool(true)
int(42)
end
