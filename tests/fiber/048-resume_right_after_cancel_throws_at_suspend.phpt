--TEST--
Fiber: a resume() right after its coroutine is cancelled delivers the cancellation at the body's Fiber::suspend(), the code after it does not run, and resume() throws what the body lets out
--DESCRIPTION--
The case fiber/023 and 028 leave to %a, with no suspend() between the cancel and the resume(), unlike 024.
--FILE--
<?php

use Async\AsyncCancellation;
use function Async\await;
use function Async\spawn;

$coroutine = spawn(function () {
    $fiber = new Fiber(function () {
        try {
            Fiber::suspend();
            echo "body: after suspend\n";
            throw new Exception("the body's own error");
        } catch (AsyncCancellation $cancellation) {
            echo "body: ", $cancellation->getMessage(), " at suspend()\n";
            throw $cancellation;
        }
    });

    $fiber->start();
    $fiber->getCoroutine()->cancel(new AsyncCancellation("cancelled"));

    try {
        $fiber->resume();
        echo "resume() returned\n";
    } catch (Throwable $error) {
        echo "resume(): ", get_class($error), ": ", $error->getMessage(), "\n";
    }

    echo "fiber terminated: ", var_export($fiber->isTerminated(), true), "\n";
});

await($coroutine);
echo "end\n";
?>
--EXPECT--
body: cancelled at suspend()
resume(): Async\AsyncCancellation: cancelled
fiber terminated: true
end
