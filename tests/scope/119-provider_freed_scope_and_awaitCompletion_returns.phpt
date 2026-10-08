--TEST--
Scope: a provider returning a Scope whose scope was freed makes spawn_with() throw, where TrueAsync spawns in the current scope; awaitCompletion() returns at once on a closed scope, a freed one, and one whose child scopes have no coroutine
--FILE--
<?php

use Async\Scope;
use function Async\spawn_with;

final class FixedProvider implements Async\ScopeProvider
{
    public function __construct(private Scope $scope) {}

    public function provideScope(): ?Scope
    {
        return $this->scope;
    }
}

$closed = new Scope();
$closed->dispose();
$closed->awaitCompletion(Async\timeout(1000));
echo "closed: returned\n";

// The cancelled child scope is freed with its parent once its member ends; its object stays.
$parent = new Scope();
$freed = Scope::inherit($parent);
$member = $freed->spawn(fn() => Async\delay(10000));
unset($parent);
$freed->cancel();
while (!$member->isCompleted()) {
    Async\suspend();
}

try {
    spawn_with(new FixedProvider($freed), fn() => print("ran\n"));
} catch (Async\AsyncException $e) {
    echo $e->getMessage(), "\n";
}

$freed->awaitCompletion(Async\timeout(1000));
echo "freed: returned\n";

// A coroutine that would run first if the wait suspended.
Async\spawn(fn() => print("other coroutine\n"));
$idle = new Scope();
$idle_child = Scope::inherit($idle);
$idle->awaitCompletion(Async\timeout(1000));
echo "idle child scope: returned\n";

?>
--EXPECT--
closed: returned
Scope object has been disposed
freed: returned
idle child scope: returned
other coroutine
