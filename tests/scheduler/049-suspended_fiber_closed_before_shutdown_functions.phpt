--TEST--
A fiber left suspended when main ends is closed with a graceful exit before the shutdown functions run, unlike plain PHP: a shutdown function can no longer resume it
--FILE--
<?php
$fiber = new Fiber(function () {
    try {
        $value = Fiber::suspend();
        echo "not reached: $value\n";
    } finally {
        echo "fiber finally\n";
    }
});
$fiber->start();

register_shutdown_function(function () use ($fiber) {
    var_dump($fiber->isTerminated());

    try {
        $fiber->resume(1);
    } catch (FiberError $error) {
        echo $error->getMessage(), "\n";
    }
});

echo "main end\n";
?>
--EXPECT--
main end
fiber finally
bool(true)
Cannot resume a fiber that is not suspended
