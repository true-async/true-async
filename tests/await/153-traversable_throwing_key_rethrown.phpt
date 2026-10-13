--TEST--
await_all() over a Traversable whose key() throws rethrows it
--FILE--
<?php
use Async\Future;
use function Async\await_all;

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
?>
--EXPECT--
Exception: key
