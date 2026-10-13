--TEST--
Pool: a negative healthcheckInterval is refused with ValueError
--FILE--
<?php

use Async\Pool;

try {
    new Pool(factory: fn() => 1, healthcheckInterval: -1);
} catch (ValueError $exception) {
    echo $exception->getMessage(), "\n";
}
?>
--EXPECT--
Async\Pool::__construct(): Argument #8 ($healthcheckInterval) must be >= 0
