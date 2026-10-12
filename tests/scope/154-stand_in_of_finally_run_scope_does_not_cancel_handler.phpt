--TEST--
Scope: cancel() through the stand-in object a SpawnStrategy's hook gets for a finally handler's run scope leaves the waiting handler to finish
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;
use Async\SpawnStrategy;
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

$ended = false;
$scope = new Scope();
$scope->spawn(function () {
    Async\current_coroutine()->finally(function () {
        spawn_with(new CurrentScopeStrategy(), function () {
        });

        while (!CurrentScopeStrategy::$go) {
            suspend();
        }

        echo "finally ends\n";
        $GLOBALS['ended'] = true;
    });
});

while (CurrentScopeStrategy::$seen === null) {
    suspend();
}

CurrentScopeStrategy::$seen->cancel();
CurrentScopeStrategy::$go = true;

// A delay could end before the finally handler has run on: a timer due when main parks wakes it in place
while (!$ended) {
    suspend();
}

echo "end\n";
?>
--EXPECT--
finally ends
end
