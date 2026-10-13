--TEST--
iterate(): a callback's exception that a coroutine it spawned rethrows is thrown once, not also printed as uncaught
--FILE--
<?php

use function Async\delay;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

$exception = new Exception('callback');
$started = false;

try {
    iterate([1], function () use ($exception, &$started) {
        spawn(function () use ($exception, &$started) {
            try {
                $started = true;
                delay(10000);
            } finally {
                echo "child rethrows\n";
                throw $exception;
            }
        });

        while (!$started) {
            suspend();
        }

        throw $exception;
    });
} catch (Exception $caught) {
    echo "caught: ", $caught->getMessage(), ", same: ", var_export($caught === $exception, true), "\n";
}

echo "end\n";
?>
--EXPECT--
child rethrows
caught: callback, same: true
end
