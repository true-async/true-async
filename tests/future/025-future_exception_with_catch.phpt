--TEST--
Future: no warning when exception is caught
--XFAIL--
Not implemented yet: S5.2 of dev/PLAN.md
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\spawn;

spawn(function() {
    $state = new FutureState();
    $future = new Future($state);
    $state->error(new Exception("Test error"));

    // catch() handles the exception - no warning
    $caught = $future->catch(fn($e) => "caught: " . $e->getMessage());

    echo $caught->await() . "\n";
});

echo "Done\n";

?>
--EXPECT--
Done
caught: Test error
