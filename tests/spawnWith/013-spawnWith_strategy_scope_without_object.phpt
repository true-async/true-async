--TEST--
Async\spawn_with: a SpawnStrategy whose scope has no object sees one stand-in Scope while anything holds it
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;
use Async\SpawnStrategy;
use function Async\await;
use function Async\spawn;
use function Async\spawn_with;

class GlobalScopeStrategy implements SpawnStrategy
{
    public ?Scope $seen = null;

    public function provideScope(): ?Scope
    {
        return null;
    }

    public function beforeCoroutineEnqueue(Coroutine $coroutine, Scope $scope): array
    {
        echo "before: ", var_export($scope === $this->seen, true), "\n";
        $this->seen = $scope;
        return [];
    }

    public function afterCoroutineEnqueue(Coroutine $coroutine, Scope $scope): void
    {
    }
}

// A coroutine of the global scope runs first, so the global scope has a member when the strategy's
// coroutine joins it.
spawn(fn() => null);

$strategy = new GlobalScopeStrategy();
await(spawn_with($strategy, fn() => print("run\n")));
await(spawn_with($strategy, fn() => print("run again\n")));

// Held, the stand-in stays the global scope's object.
var_dump($strategy->seen->isClosed());
await($strategy->seen->spawn(fn() => print("spawned through the stand-in\n")));

?>
--EXPECT--
before: false
run
before: true
run again
bool(false)
spawned through the stand-in
