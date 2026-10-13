--TEST--
iterate(): an exception two coroutines the callback spawned rethrow is thrown once, not also printed as uncaught
--FILE--
<?php

use function Async\delay;
use function Async\iterate;
use function Async\spawn;

$exception = new Exception('shared');

try {
    iterate([1], function () use ($exception) {
        for ($i = 0; $i < 2; $i++) {
            spawn(function () use ($exception) {
                try {
                    delay(10000);
                } finally {
                    throw $exception;
                }
            });
        }

        delay(1);
        throw new Exception('callback');
    });
} catch (Exception $caught) {
    echo "caught: ", $caught->getMessage(), ", previous: ", $caught->getPrevious()?->getMessage(), "\n";
}

echo "end\n";
?>
--EXPECT--
caught: callback, previous: shared
end
