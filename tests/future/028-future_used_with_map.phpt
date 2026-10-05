--TEST--
Future: no warning when map() is called
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
    $state->complete(21);

    // map() marks future as used
    $mapped = $future->map(fn($v) => $v * 2);
    echo $mapped->await() . "\n";
});

echo "Done\n";

?>
--EXPECT--
Done
42
