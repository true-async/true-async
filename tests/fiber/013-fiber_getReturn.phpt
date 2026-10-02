--TEST--
Fiber.getReturn() method
--XFAIL--
Not implemented yet: S3.9 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;
use function Async\await;

echo "Test: Fiber getReturn\n";

$coroutine = spawn(function() {
    $fiber = new Fiber(function() {
        Fiber::suspend("suspended");
        return "final result";
    });

    $fiber->start();
    $fiber->resume();

    $result = $fiber->getReturn();
    echo "getReturn: " . $result . "\n";

    return "done";
});

await($coroutine);
echo "Test completed\n";
?>
--EXPECT--
Test: Fiber getReturn
getReturn: final result
Test completed
