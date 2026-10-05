--TEST--
Future::finally() - executes on error
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

    $result = $future->finally(function() {
        echo "Finally executed\n";
    });

    $state->error(new Exception("Test error"));

    try {
        await($result);
    } catch (Exception $e) {
        echo "Error: " . $e->getMessage() . "\n";
    }
});

await($coroutine);

?>
--EXPECT--
Finally executed
Error: Test error
