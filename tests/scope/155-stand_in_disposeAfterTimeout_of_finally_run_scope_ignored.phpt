--TEST--
Scope: disposeAfterTimeout() through the stand-in object a SpawnStrategy's hook gets for a finally handler's run scope does nothing, so the waiting handler finishes
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;
use Async\SpawnStrategy;
use function Async\delay;
use function Async\spawn_with;
use function Async\suspend;

final class CurrentScopeStrategy implements SpawnStrategy
{
    public static ?Scope $seen = null;
    public static bool $go = false;

    public function provideScope(): ?Scope
    {
        return null;
    }

    public function beforeCoroutineEnqueue(Coroutine $coroutine, Scope $scope): array
    {
        self::$seen = $scope;
        return [];
    }

    public function afterCoroutineEnqueue(Coroutine $coroutine, Scope $scope): void
    {
    }
}

$scope = new Scope();
$scope->spawn(function () {
    Async\current_coroutine()->finally(function () {
        spawn_with(new CurrentScopeStrategy(), function () {
        });

        while (!CurrentScopeStrategy::$go) {
            suspend();
        }

        echo "finally ends\n";
    });
});

while (CurrentScopeStrategy::$seen === null) {
    suspend();
}

CurrentScopeStrategy::$seen->disposeAfterTimeout(0);
CurrentScopeStrategy::$go = true;
delay(10);
echo "end\n";
?>
--EXPECT--
finally ends
end
