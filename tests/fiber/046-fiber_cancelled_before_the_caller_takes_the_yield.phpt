--TEST--
A fiber whose coroutine is cancelled after it yielded, before its waiting caller took the value, leaves the value and no error to that caller
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$fiberCoroutine = null;

$fiber = new Fiber(function () use (&$fiberCoroutine) {
    $fiberCoroutine = Async\current_coroutine();
    Fiber::suspend();
    Fiber::suspend('yielded');
});
$fiber->start();

$waiter = spawn(function () use ($fiber) {
    echo "waiter got ", $fiber->resume(), "\n";
});

$canceller = spawn(function () use (&$fiberCoroutine) {
    Async\suspend();
    $fiberCoroutine->asHiPriority();
    $fiberCoroutine->cancel();
});

await($waiter);
await($canceller);
echo "fiber terminated: ", var_export($fiber->isTerminated(), true), "\n";
?>
--EXPECT--
waiter got yielded
fiber terminated: true
