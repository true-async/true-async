--TEST--
Future::map() - callback with no return value
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
        echo "Processed: $value\n";
        // No return
    });

    $state->complete("test");

    return await($mapped);
});

$result = await($coroutine);
echo "Result: " . ($result === null ? "NULL" : $result) . "\n";

?>
--EXPECT--
Processed: test
Result: NULL
