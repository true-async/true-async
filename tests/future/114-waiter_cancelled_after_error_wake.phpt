--TEST--
A Future::await() waiter woken by the Future's error and cancelled before it runs leaves the error handled: nothing reports it at the end
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\spawn;
use function Async\suspend;

$state = new FutureState();
$future = new Future($state);

$waiter = spawn(function () use ($future) {
    try {
        $future->await();
    } finally {
        echo "waiter: finally\n";
    }
});

suspend();
$state->error(new RuntimeException("failed"));
$waiter->cancel();
suspend();
var_dump($waiter->isCancelled());
unset($future, $state);
echo "end\n";

?>
--EXPECT--
waiter: finally
bool(true)
end
