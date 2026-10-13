--TEST--
Async\spawn_with: two SpawnStrategy hooks wait at once on one stand-in Scope; the first to return leaves it usable for the other
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
use function Async\suspend;

class GlobalScopeStrategy implements SpawnStrategy
{
    public ?Scope $seen = null;
    private static int $hooks_waiting = 0;

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
        /* Under load one hook's timer could end before the other hook began */
        self::$hooks_waiting++;
        while (self::$hooks_waiting < 2) {
            suspend();
        }

        delay(10);
        echo "after the wait: ", var_export($scope->isClosed(), true), "\n";
    }
}

// A coroutine of the global scope runs first, so the global scope has a member when the strategy's
// coroutine joins it.
spawn(fn() => null);

await_all([
    spawn(fn() => await(spawn_with(new GlobalScopeStrategy(), fn() => null))),
    spawn(fn() => await(spawn_with(new GlobalScopeStrategy(), fn() => null))),
]);

?>
--EXPECT--
before: false
before: false
after the wait: false
after the wait: false
