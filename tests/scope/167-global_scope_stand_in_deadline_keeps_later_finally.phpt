--TEST--
Scope: after a disposeAfterTimeout() fire through an unsafe stand-in of the global scope cancels main, a later coroutine's Coroutine::finally() handler is called
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;
use Async\SpawnStrategy;
use function Async\delay;
use function Async\spawn;
use function Async\spawn_with;
use function Async\suspend;

final class GlobalScopeStrategy implements SpawnStrategy
{
    public static ?Scope $seen = null;

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

spawn_with(new GlobalScopeStrategy(), function () {
    delay(100000);
});

while (GlobalScopeStrategy::$seen === null) {
    suspend();
}

GlobalScopeStrategy::$seen->asNotSafely()->disposeAfterTimeout(10);

try {
    delay(60);
} catch (Async\AsyncCancellation $cancellation) {
    echo "main cancelled\n";
}

spawn(function () {
})->finally(function () {
    echo "finally runs\n";
});
delay(10);
echo "end\n";
?>
--EXPECT--
main cancelled
finally runs
end
