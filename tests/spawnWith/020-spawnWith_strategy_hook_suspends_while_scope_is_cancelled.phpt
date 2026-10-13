--TEST--
Async\spawn_with: a SpawnStrategy hook that suspends while its scope goes
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;
use Async\SpawnStrategy;
use function Async\spawn_with;
use function Async\suspend;

class SuspendingStrategy implements SpawnStrategy
{
    public function __construct(private ?Scope $scope) {}

    public function provideScope(): ?Scope
    {
        return $this->scope;
    }

    public function beforeCoroutineEnqueue(Coroutine $coroutine, Scope $scope): array
    {
        return [];
    }

    public function afterCoroutineEnqueue(Coroutine $coroutine, Scope $scope): void
    {
        $scope->cancel();
        suspend();
        var_dump($scope->isClosed());
    }
}

// The scope is cancelled and goes with its only coroutine while the hook waits.
$coroutine = spawn_with(new SuspendingStrategy(new Scope()), fn() => print("must not run\n"));
var_dump($coroutine->isCompleted(), $coroutine->isCancelled());

echo "end\n";

?>
--EXPECT--
bool(true)
bool(true)
bool(true)
end
