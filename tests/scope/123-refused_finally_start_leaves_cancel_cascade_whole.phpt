--TEST--
Scope: a finally run refused for want of a stack runs no PHP code inside the cancel's cascade, which closes every child scope; the scope's disposal starts the handlers once a stack can be had
--INI--
fiber.stack_size=64G
--FILE--
<?php

use Async\Scope;

class Strategy implements Async\SpawnStrategy
{
    public ?Scope $scope = null;

    public function provideScope(): ?Scope
    {
        return null;
    }

    public function beforeCoroutineEnqueue($coroutine, $scope): array
    {
        $this->scope = $scope;
        return [];
    }

    public function afterCoroutineEnqueue($coroutine, $scope): void {}
}

// Released with the handlers: it drops the object of the scope the cascade is at, which disposes that
// scope, so it must not run inside the cascade's loop.
class Dropper
{
    public function __destruct()
    {
        echo "dropper destroyed\n";
        unset($GLOBALS["children"][0], $GLOBALS["first"]);
    }
}

// The global scope's stand-in, from a spawn the missing stack refuses after the first hook.
$strategy = new Strategy();

try {
    Async\spawn_with($strategy, fn() => null);
} catch (Exception $e) {
    echo "spawn: ", $e->getMessage(), "\n";
}

$children = [];

for ($i = 0; $i < 4; $i++) {
    $children[] = Scope::inherit();
}

$dropper = new Dropper();
$children[0]->finally(function () use ($dropper) {
    echo "finally handler\n";
});
unset($dropper);
$first = $children[0];

try {
    $strategy->scope->cancel();
} catch (Exception $e) {
    echo "cancel: ", $e->getMessage(), "\n";
}

foreach ($children as $index => $child) {
    echo $index, " closed: ", var_export($child->isClosed(), true), "\n";
}

ini_set("fiber.stack_size", "2M");
unset($first, $children);
echo "end\n";

?>
--EXPECTF--
spawn: Fiber stack allocate failed: %s
cancel: Fiber stack allocate failed: %s
0 closed: true
1 closed: true
2 closed: true
3 closed: true
end
finally handler
dropper destroyed
