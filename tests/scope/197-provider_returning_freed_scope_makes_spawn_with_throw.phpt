--TEST--
Scope: a provider returning a Scope whose scope was freed makes spawn_with() throw, where TrueAsync spawns in the current scope
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

?>
--EXPECT--
Scope object has been disposed
