--TEST--
An exception from a Traversable's valid(), current(), key() or next() stops the iterator core's walk, and the last worker ends with it; the walk starts with rewind()
--FILE--
<?php

use Async\Scope;
use function TrueAsync\Test\iterate;

final class Throwing implements Iterator
{
    private int $position = 0;

    public function __construct(private string $method) {}

    private function check(string $method): void
    {
        if ($this->method === $method && $this->position === 1) {
            throw new Exception("$method throws");
        }
    }

    public function valid(): bool { $this->check('valid'); return $this->position < 3; }
    public function current(): mixed { $this->check('current'); return $this->position * 10; }
    public function key(): mixed { $this->check('key'); return "k$this->position"; }
    public function next(): void { $this->check('next'); $this->position++; }
    public function rewind(): void { echo "rewind\n"; $this->position = 0; }
}

foreach (['valid', 'current', 'key', 'next'] as $method) {
    echo "--- $method\n";
    $scope = new Scope();
    $scope->setChildScopeExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $error) {
        echo "error: ", $error->getMessage(), "\n";
    });
    $scope->spawn(function () use ($method) {
        iterate(new Throwing($method), function ($value, $key) {
            echo "$key => $value\n";
        }, 1);
    });
    $scope->awaitCompletion(Async\timeout(2000));
}

?>
--EXPECT--
--- valid
rewind
k0 => 0
error: valid throws
--- current
rewind
k0 => 0
error: current throws
--- key
rewind
k0 => 0
error: key throws
--- next
rewind
k0 => 0
error: next throws
