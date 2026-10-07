--TEST--
Async\spawnWith: with inherited scope
--XFAIL--
Not implemented yet: S9.2 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn_with;
use function Async\await;
use Async\Scope;

echo "start\n";

$parentScope = new Scope();
$childScope = Scope::inherit($parentScope);

$coroutine = spawn_with($childScope, function() {
    echo "coroutine in child scope\n";
    return "inherited scope result";
});

$result = await($coroutine);
echo "result: $result\n";

echo "end\n";

?>
--EXPECT--
start
coroutine in child scope
result: inherited scope result
end