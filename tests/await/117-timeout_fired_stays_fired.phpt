--TEST--
A fired Timeout stays fired, and each use gets a TimeoutException of its own: what the engine chains onto one does not reach the next
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use Async\OperationCanceledException;
use function Async\await;
use function Async\await_all;
use function Async\timeout;

$timeout = timeout(10);
$never = new FutureState();
$never->ignore();

try {
    try {
        throw new LogicException("unrelated");
    } finally {
        await(new Future($never), $timeout);
    }
} catch (OperationCanceledException $e) {
    $first = $e->getPrevious();
    echo "first: ", $first->getMessage(), ", chained: ", $first->getPrevious()->getMessage(), "\n";
}

var_dump($timeout->isCompleted(), $timeout->isCancelled());

try {
    await_all([new Future($never)], $timeout);
} catch (OperationCanceledException $e) {
    $later = $e->getPrevious();
    echo "later: ", $later->getMessage(), ", same: ", var_export($later === $first, true),
        ", chained: ", var_export($later->getPrevious(), true), "\n";
}

?>
--EXPECT--
first: Timeout occurred after 10 milliseconds, chained: unrelated
bool(true)
bool(false)
later: Timeout occurred after 10 milliseconds, same: false, chained: NULL
