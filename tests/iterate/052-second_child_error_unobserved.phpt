--TEST--
iterate(): a second spawned coroutine's error goes to the unobserved exceptions, the first is thrown
--FILE--
<?php

use function Async\delay;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

try {
    iterate([1], function () {
        spawn(function () {
            try {
                delay(10000);
            } catch (Async\AsyncCancellation) {
                throw new RuntimeException('second child failed');
            }
        });
        spawn(function () {
            suspend();
            throw new RuntimeException('first child failed');
        });
        delay(10000);
    }, cancelPending: false);
    echo "iterate returned\n";
} catch (RuntimeException $exception) {
    echo "caught: ", $exception->getMessage(), ", previous: ", var_export($exception->getPrevious(), true), "\n";
}

echo "end\n";
?>
--EXPECTF--
caught: first child failed, previous: NULL
end

Fatal error: Uncaught RuntimeException: second child failed in %s:%d
Stack trace:
#0 [internal function]: {closure:%s}()
#1 {main}
  thrown in %s on line %d
