--TEST--
iterate(): a spawned coroutine that ends in the turn the last worker gives it is not cancelled
--FILE--
<?php

use function Async\iterate;
use function Async\spawn;

iterate([1], function () {
    spawn(function () {
        echo "child ends\n";
    });
});

echo "iterate returned\n";
?>
--EXPECT--
child ends
iterate returned
