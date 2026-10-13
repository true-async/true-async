--TEST--
iterate(): the previous of a callback's exception, rethrown by a coroutine the callback spawned, is not printed as uncaught
--FILE--
<?php

use function Async\delay;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

$inner = new Exception('inner');
$outer = new RuntimeException('outer', 0, $inner);
$started = false;

try {
    iterate([1], function () use ($inner, $outer, &$started) {
        spawn(function () use ($inner, &$started) {
            try {
                $started = true;
                delay(10000);
            } finally {
                echo "child rethrows\n";
                throw $inner;
            }
        });

        while (!$started) {
            suspend();
        }

        throw $outer;
    });
} catch (RuntimeException $caught) {
    echo "caught: ", $caught->getMessage(), ", previous is inner: ", var_export($caught->getPrevious() === $inner, true), "\n";
}

echo "end\n";
?>
--EXPECT--
child rethrows
caught: outer, previous is inner: true
end
