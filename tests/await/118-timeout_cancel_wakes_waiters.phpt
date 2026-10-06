--TEST--
Timeout::cancel() ends it for good: a parked waiter and every later use throw with its argument as the previous (D32 rule 7)
--FILE--
<?php

use Async\AsyncCancellation;
use Async\FutureState;
use Async\Future;
use Async\OperationCanceledException;
use function Async\await;
use function Async\spawn;
use function Async\suspend;
use function Async\timeout;

$timeout = timeout(60000);
$never = new FutureState();
$never->ignore();

$waiter = spawn(function () use ($never, $timeout) {
    try {
        await(new Future($never), $timeout);
    } catch (OperationCanceledException $e) {
        echo "waiter: ", $e->getPrevious()->getMessage(), "\n";
    }
});

suspend();
$timeout->cancel(new AsyncCancellation("stop"));
$timeout->cancel();
await($waiter);

var_dump($timeout->isCompleted(), $timeout->isCancelled());

try {
    await(new Future($never), $timeout);
} catch (OperationCanceledException $e) {
    echo "later: ", $e->getPrevious()->getMessage(), "\n";
}

$bare = timeout(60000);
$bare->cancel();

try {
    await(new Future($never), $bare);
} catch (OperationCanceledException $e) {
    echo "bare: previous ", var_export($e->getPrevious(), true), "\n";
}

?>
--EXPECT--
waiter: stop
bool(true)
bool(true)
later: stop
bare: previous NULL
