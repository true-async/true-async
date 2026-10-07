--TEST--
await_all() over a Traversable: null keys take the next index, a null item is skipped, and a throwing key() or getIterator() is rethrown
--FILE--
<?php
use Async\Future;
use function Async\await_all;

var_dump(await_all((function () {
    yield null => Future::completed('a');
    yield null => Future::completed('b');
    yield null;
    yield 'k' => Future::completed('c');
})())[0]);

$keyThrows = new class implements Iterator {
    private int $position = 0;

    public function current(): mixed
    {
        $future = Future::completed(1);
        $future->ignore();
        return $future;
    }

    public function key(): mixed
    {
        throw new Exception("key");
    }

    public function next(): void
    {
        $this->position++;
    }

    public function rewind(): void
    {
        $this->position = 0;
    }

    public function valid(): bool
    {
        return $this->position < 1;
    }
};

try {
    await_all($keyThrows);
} catch (Throwable $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

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
array(3) {
  [0]=>
  string(1) "a"
  [1]=>
  string(1) "b"
  ["k"]=>
  string(1) "c"
}
Exception: key
Exception: getIterator
