--TEST--
Async\spawn_with: a fatal error in a SpawnStrategy hook of the global scope ends the request cleanly
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;
use Async\SpawnStrategy;
use function Async\spawn_with;

class FatalStrategy implements SpawnStrategy
{
    public ?Scope $seen = null;

    public function provideScope(): ?Scope
    {
        return null;
    }

    public function beforeCoroutineEnqueue(Coroutine $coroutine, Scope $scope): array
    {
        $this->seen = $scope;
        eval("function hook_failed() {} function hook_failed() {}");
    }

    public function afterCoroutineEnqueue(Coroutine $coroutine, Scope $scope): void {}
}

$strategy = new FatalStrategy();
spawn_with($strategy, fn() => print("must not run\n"));

?>
--EXPECTF--
Fatal error: Cannot redeclare function hook_failed() (previously declared in %s) in %s on line 1
