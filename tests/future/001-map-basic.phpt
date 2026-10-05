--TEST--
Future::map() - basic transformation
--XFAIL--
Not implemented yet: S5.2 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use Async\FutureState;
use Async\Future;

$coroutine = spawn(function() {
    $state = new FutureState();
    $future = new Future($state);

    $mapped = $future->map(function($value) {
        echo "Mapped: $value\n";
        return $value * 2;
    });

    $state->complete(21);

    return await($mapped);
});

echo "Result: " . await($coroutine) . "\n";

?>
--EXPECT--
Mapped: 21
Result: 42
