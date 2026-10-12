--TEST--
Pool: no object exists without its constructor: unserialize() and newInstanceWithoutConstructor() are refused
--EXTENSIONS--
true_async
--FILE--
<?php

use Async\Pool;

/* TrueAsync's unserialize() built an object without a pool, whose every method had to answer that. Here the
   class is not serializable, and Reflection refuses a final internal class with its own object handler. */
try {
    unserialize('O:10:"Async\Pool":0:{}');
    echo "unserialize: returned\n";
} catch (Exception $e) {
    echo "unserialize: ", $e->getMessage(), "\n";
}

try {
    (new ReflectionClass(Pool::class))->newInstanceWithoutConstructor();
    echo "newInstanceWithoutConstructor: returned\n";
} catch (ReflectionException $e) {
    echo "newInstanceWithoutConstructor: ", $e->getMessage(), "\n";
}

echo "Done\n";
?>
--EXPECT--
unserialize: Unserialization of 'Async\Pool' is not allowed
newInstanceWithoutConstructor: Class Async\Pool is an internal class marked as final that cannot be instantiated without invoking its constructor
Done
