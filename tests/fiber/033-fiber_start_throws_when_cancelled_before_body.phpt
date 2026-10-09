--TEST--
Fiber: the fiber's coroutine is cancelled before its body runs; start() throws that cancellation and the fiber is terminated
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use Async\AsyncCancellation;

$fiber = new Fiber(function () {
    echo "body\n";
});

$starter = spawn(function () use ($fiber) {
    try {
        $fiber->start();
        echo "start returned\n";
    } catch (AsyncCancellation $e) {
        echo "start threw: ", $e->getMessage(), "\n";
    }
});

// Queued behind the starter: it runs after start() queued the body and before the body
spawn(function () use ($fiber) {
    $fiber->getCoroutine()->cancel(new AsyncCancellation("before body"));
});

await($starter);
var_dump($fiber->isTerminated());
?>
--EXPECT--
start threw: before body
bool(true)
