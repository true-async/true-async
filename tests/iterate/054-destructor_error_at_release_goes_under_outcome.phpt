--TEST--
iterate(): a destructor's error when the walk is released goes under the caller's cancellation
--FILE--
<?php

use Async\Scope;
use function Async\await;
use function Async\spawn;
use function Async\iterate;

class ThrowingIterator implements Iterator
{
    public function __destruct()
    {
        throw new Exception('destructor');
    }

    public function current(): mixed { return 1; }
    public function key(): mixed { return 0; }
    public function next(): void {}
    public function rewind(): void {}
    public function valid(): bool { return false; }
}

class Aggregate implements IteratorAggregate
{
    public function getIterator(): Iterator
    {
        return new ThrowingIterator();
    }
}

$scope = new Scope();

$caller = $scope->spawn(function () {
    try {
        iterate(new Aggregate(), function () {
            echo "callback\n";
        });
    } catch (Throwable $exception) {
        echo get_class($exception), ": ", $exception->getMessage(), ", previous: ",
            $exception->getPrevious()?->getMessage(), "\n";
    }
});

// The first worker is cancelled before it runs, so the caller's release disposes the walk.
spawn(fn () => $scope->cancel());

await($caller);
echo "end\n";
?>
--EXPECT--
Async\AsyncCancellation: Scope was cancelled, previous: destructor
end
