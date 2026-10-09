--TEST--
Fiber::getCoroutine(): a suspended fiber's coroutine is cancelled, then its parent while the parent waits in resume(); both end cancelled, await() gets the parent's cancellation and the fiber's is dropped, not chained
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use Async\AsyncCancellation;

$ready = false;

$parent = spawn(function () use (&$ready) {
    $fiber = new Fiber(function () {
        try {
            Fiber::suspend();
            echo "fiber not cancelled\n";
        } finally {
            echo "fiber finally\n";
        }
    });

    $fiber->start();
    $fiber->getCoroutine()->cancel(new AsyncCancellation("fiber cancel"));
    echo "fiber coroutine cancelled\n";
    $ready = true;

    try {
        $fiber->resume();
        echo "parent not cancelled\n";
    } finally {
        echo "parent finally\n";
        var_dump($fiber->isTerminated(), $fiber->getCoroutine()->isCancelled());
    }
});

while (!$ready) {
    suspend();
}

// The fiber's coroutine is already queued, so it runs before the parent: the run queue is first in, first out
$parent->cancel(new AsyncCancellation("parent cancel"));
echo "parent cancelled\n";

try {
    await($parent);
} catch (AsyncCancellation $e) {
    echo "main caught: ", $e->getMessage(), "\n";
    // The fiber's cancellation reaches the parent after its own and is dropped, not chained
    var_dump($e->getPrevious());
}

var_dump($parent->isCancelled());
?>
--EXPECT--
fiber coroutine cancelled
parent cancelled
fiber finally
parent finally
bool(true)
bool(true)
main caught: parent cancel
NULL
bool(true)
