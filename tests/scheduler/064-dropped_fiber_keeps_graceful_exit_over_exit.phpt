--TEST--
A dropped suspended Fiber waits with its graceful exit when exit() cancels every coroutine: the exit's cancellation does not replace it, and the fiber's finally runs once
--FILE--
<?php
$fiber = new Fiber(function () {
    try {
        Fiber::suspend();
    } finally {
        echo "fiber finally\n";
    }
});
$fiber->start();
unset($fiber);

echo "main exit\n";
exit();
?>
--EXPECT--
main exit
fiber finally
