--TEST--
Fiber::getCoroutine() method
--XFAIL--
Not implemented yet: S10.4 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;
use function Async\await;

// Test with coroutine
$c = spawn(function() {
    $f = new Fiber(function() {
        Fiber::suspend();
    });

    $f->start();

    $coro = $f->getCoroutine();
    echo "Has coroutine: " . ($coro !== null ? "yes" : "no") . "\n";
    echo "Has ID: " . ($coro->getId() > 0 ? "yes" : "no") . "\n";
    echo "Is started: " . ($coro->isStarted() ? "yes" : "no") . "\n";
    echo "Is suspended: " . ($coro->isSuspended() ? "yes" : "no") . "\n";

    $f->resume();
});

await($c);

echo "OK\n";
?>
--EXPECT--
Has coroutine: yes
Has ID: yes
Is started: yes
Is suspended: yes
OK
