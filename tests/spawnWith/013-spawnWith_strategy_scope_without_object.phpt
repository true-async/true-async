--TEST--
Async\spawn_with: a SpawnStrategy whose scope has no object sees one stand-in Scope while anything holds it
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;
use Async\SpawnStrategy;
use function Async\await;
use function Async\await_all;
use function Async\delay;
use function Async\spawn;
use function Async\spawn_with;

class GlobalScopeStrategy implements SpawnStrategy
{
    public ?Scope $seen = null;

    public function __construct(private bool $waits = false) {}

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
        if ($this->waits) {
            delay(10);
            echo "after the wait: ", var_export($scope->isClosed(), true), "\n";
        }
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

// Two hooks wait at once on one stand-in; the first to return leaves it usable for the other.
$strategy = null;
await_all([
    spawn(fn() => await(spawn_with(new GlobalScopeStrategy(true), fn() => null))),
    spawn(fn() => await(spawn_with(new GlobalScopeStrategy(true), fn() => null))),
]);

?>
--EXPECT--
before: false
run
before: true
run again
bool(false)
spawned through the stand-in
before: false
before: false
after the wait: false
after the wait: false
