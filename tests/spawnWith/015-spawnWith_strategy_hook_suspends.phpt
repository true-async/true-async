--TEST--
Async\spawn_with: a SpawnStrategy hook that suspends while the coroutine finishes, or while its scope goes
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
    public function __construct(private ?Scope $scope, private string $mode) {}

    public function provideScope(): ?Scope
    {
        return $this->scope;
    }

    public function beforeCoroutineEnqueue(Coroutine $coroutine, Scope $scope): array
    {
        if ($this->mode === 'cancel before') {
            $coroutine->cancel();
            suspend();
        }

        return [];
    }

    public function afterCoroutineEnqueue(Coroutine $coroutine, Scope $scope): void
    {
        if ($this->mode === 'run after') {
            suspend();
        } elseif ($this->mode === 'cancel scope after') {
            $scope->cancel();
            suspend();
            var_dump($scope->isClosed());
        }
    }
}

// The coroutine runs and finishes while the hook waits; spawn_with still returns it.
$coroutine = spawn_with(new SuspendingStrategy(null, 'run after'), fn() => 42);
var_dump($coroutine->isCompleted(), await($coroutine));

// The coroutine is cancelled and finishes unrun before it would be queued.
$coroutine = spawn_with(new SuspendingStrategy(null, 'cancel before'), fn() => print("must not run\n"));
var_dump($coroutine->isCompleted(), $coroutine->isCancelled());

// The scope is cancelled and goes with its only coroutine while the hook waits.
$coroutine = spawn_with(new SuspendingStrategy(new Scope(), 'cancel scope after'), fn() => print("must not run\n"));
var_dump($coroutine->isCompleted(), $coroutine->isCancelled());

echo "end\n";

?>
--EXPECT--
bool(true)
int(42)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
end
