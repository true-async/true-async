--TEST--
A coroutine that yields in a loop keeps the run queue from emptying, and the tick's throttled poll still fires a due Timer op
--FILE--
<?php
use function Async\spawn;
use TrueAsync\Test;

$fired = false;
spawn(function () use (&$fired) {
    Test\reactor_wait(20);
    $fired = true;
});

$started = hrtime(true);

while (!$fired && hrtime(true) - $started < 5 * 1000000000) {
    Async\suspend();
}

echo "fired while main yields: ", var_export($fired, true), "\n";
?>
--EXPECT--
fired while main yields: true
