--TEST--
getAwaitingInfo() names a Timeout token, and a wait that ends in place never arms it
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\await_all;
use function Async\spawn;
use function Async\suspend;
use function Async\timeout;

$never = new FutureState();
$never->ignore();
$timeout = timeout(60000);

$waiter = spawn(function () use ($never, $timeout) {
    try {
        await(new Future($never), $timeout);
    } catch (Throwable $e) {
        echo get_class($e), "\n";
    }
});

suspend();

foreach ($waiter->getAwaitingInfo() as $line) {
    echo $line, "\n";
}

$timeout->cancel();
await($waiter);

var_dump(await_all([Future::completed(1)], timeout(60000)));

?>
--EXPECT--
await: future
cancellation: timeout
Async\OperationCanceledException
array(2) {
  [0]=>
  array(1) {
    [0]=>
    int(1)
  }
  [1]=>
  array(0) {
  }
}
