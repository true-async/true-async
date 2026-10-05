--TEST--
Future::map() - callback throws exception
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
        echo "Processing: $value\n";
        throw new Exception("Map error");
    });

    $state->complete("test");

    try {
        await($mapped);
    } catch (Exception $e) {
        echo "Caught: " . $e->getMessage() . "\n";
    }
});

await($coroutine);

?>
--EXPECT--
Processing: test
Caught: Map error
