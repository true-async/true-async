--TEST--
iterate(): the first callback exception is thrown
--FILE--
<?php

use function Async\iterate;
use function Async\suspend;

try {
    iterate([1, 2, 3], function ($value) {
        suspend();
        if ($value !== 3) {
            throw new RuntimeException("boom $value");
        }
    });
} catch (RuntimeException $exception) {
    echo "caught: ", $exception->getMessage(), ", previous: ", var_export($exception->getPrevious(), true), "\n";
}
?>
--EXPECT--
caught: boom 1, previous: NULL
