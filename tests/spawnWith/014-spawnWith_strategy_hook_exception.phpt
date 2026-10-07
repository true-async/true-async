--TEST--
Async\spawn_with: an exception from a SpawnStrategy hook is thrown by spawn_with and cancels the coroutine before it runs
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;
use Async\SpawnStrategy;
use function Async\spawn_with;
use function Async\suspend;

class ThrowingStrategy implements SpawnStrategy
{
    public ?Coroutine $coroutine = null;

    public function __construct(private Scope $scope, private string $hook) {}

    public function provideScope(): ?Scope
    {
        return $this->scope;
    }

    public function beforeCoroutineEnqueue(Coroutine $coroutine, Scope $scope): array
    {
        $this->coroutine = $coroutine;

        if ($this->hook === 'before') {
            throw new RuntimeException("from before");
        }

        return [];
    }

    public function afterCoroutineEnqueue(Coroutine $coroutine, Scope $scope): void
    {
        if ($this->hook === 'after') {
            throw new RuntimeException("from after");
        }
    }
}

$scope = new Scope();

foreach (['before', 'after'] as $hook) {
    $strategy = new ThrowingStrategy($scope, $hook);

    try {
        spawn_with($strategy, fn() => print("must not run\n"));
    } catch (RuntimeException $e) {
        echo $hook, ": ", $e->getMessage(), "\n";
    }

    suspend();

    var_dump($strategy->coroutine->isCompleted(), $strategy->coroutine->isCancelled());
}

var_dump($scope->isFinished());

?>
--EXPECT--
before: from before
bool(true)
bool(true)
after: from after
bool(true)
bool(true)
bool(true)
