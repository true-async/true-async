--TEST--
A fiber whose last holder, its starter, is cancelled while the body waits in an await lives until the body ends: the body unwinds with Fiber::getCurrent() still the running fiber
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await;

$state = new FutureState();
$future = new Future($state);

$starter = spawn(function () use ($future) {
    $fiber = new Fiber(function () use ($future) {
        try {
            await($future);
        } finally {
            $current = Fiber::getCurrent();
            echo "finally: current is a running fiber: ", var_export($current?->isRunning(), true), "\n";
        }
    });
    $fiber->start();
});

Async\suspend();
$starter->cancel();
Async\suspend();
Async\suspend();
echo "end\n";
?>
--EXPECT--
finally: current is a running fiber: true
end
