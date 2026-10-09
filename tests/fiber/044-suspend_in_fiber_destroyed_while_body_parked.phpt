--TEST--
Fiber::suspend() in the body of a fiber destroyed while the body waited in an await throws a FiberError, as in a force-closed plain fiber
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
            try {
                Fiber::suspend();
                echo "suspend returned\n";
            } catch (FiberError $error) {
                echo $error->getMessage(), "\n";
            }
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
Cannot suspend in a force-closed fiber
end
