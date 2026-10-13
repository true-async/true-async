--TEST--
iterate(): a concurrency below 0 or above 4294967295 throws ValueError
--FILE--
<?php

use function Async\iterate;

foreach ([-1, PHP_INT_MAX] as $concurrency) {
    try {
        iterate([1], function ($value) {
            echo "callback $value\n";
        }, concurrency: $concurrency);
    } catch (ValueError $error) {
        echo $error->getMessage(), "\n";
    }
}
?>
--EXPECT--
Async\iterate(): Argument #3 ($concurrency) must be between 0 and 4294967295
Async\iterate(): Argument #3 ($concurrency) must be between 0 and 4294967295
