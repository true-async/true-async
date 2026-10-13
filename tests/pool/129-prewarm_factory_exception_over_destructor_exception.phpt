--TEST--
Pool: a pre-warm factory's exception leaves the constructor with the destructor's exception beneath it
--FILE--
<?php

use Async\Pool;

$made = 0;

try {
    new Pool(
        factory: function () use (&$made) {
            if (++$made === 2) {
                throw new LogicException("db down");
            }
            return $made;
        },
        destructor: function ($resource) {
            throw new RuntimeException("close of $resource failed");
        },
        min: 3,
    );
} catch (LogicException $exception) {
    echo $exception->getMessage(), " <- ", $exception->getPrevious()->getMessage(), "\n";
}
?>
--EXPECT--
db down <- close of 1 failed
