--TEST--
iterate(): a callback's exception that a coroutine it spawned rethrows is thrown once, not also printed as uncaught
--FILE--
<?php

use function Async\delay;
use function Async\iterate;
use function Async\spawn;

$exception = new Exception('callback');

try {
    iterate([1], function () use ($exception) {
        spawn(function () use ($exception) {
            try {
                delay(10000);
            } finally {
                echo "child rethrows\n";
                throw $exception;
            }
        });
        delay(1);
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
