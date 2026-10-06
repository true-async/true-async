--TEST--
await_all() checks the token after getIterator(), which may complete it
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\OperationCanceledException;
use function Async\await_all;

final class CancellingAggregate implements IteratorAggregate
{
    public function __construct(private FutureState $tokenState, private array $items) {}

    public function getIterator(): Iterator
    {
        echo "getIterator\n";
        $this->tokenState->error(new RuntimeException("cancelled in getIterator"));
        return new ArrayIterator($this->items);
    }
}

$tokenState = new FutureState();
$token = new Future($tokenState);
$never = new FutureState();
$never->ignore();

try {
    await_all(new CancellingAggregate($tokenState, [new Future($never)]), $token);
    echo "not cancelled\n";
} catch (OperationCanceledException $e) {
    echo get_class($e), ": ", $e->getPrevious()->getMessage(), "\n";
}

?>
--EXPECT--
getIterator
Async\OperationCanceledException: cancelled in getIterator
