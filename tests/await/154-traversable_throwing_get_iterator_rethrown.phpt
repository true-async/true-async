--TEST--
await_all() over an IteratorAggregate whose getIterator() throws rethrows it
--FILE--
<?php
use function Async\await_all;

$aggregateThrows = new class implements IteratorAggregate {
    public function getIterator(): Iterator
    {
        throw new Exception("getIterator");
    }
};

try {
    await_all($aggregateThrows);
} catch (Throwable $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}
?>
--EXPECT--
Exception: getIterator
