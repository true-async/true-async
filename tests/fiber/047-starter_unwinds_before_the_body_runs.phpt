--TEST--
A body that first runs after its starter was cancelled and unwound links its backtrace to no dead frame
--FILE--
<?php
use function Async\spawn;

$fiber = new Fiber(function () {
    $trace = (new Exception)->getTrace();
    echo "frames below the body: ", count($trace), "\n";
    Fiber::suspend();
});

$starter = spawn(function () use ($fiber) {
    $fiber->start();
});

Async\suspend();
$starter->asHiPriority();
$starter->cancel();
Async\suspend();
Async\suspend();
echo "end\n";
?>
--EXPECT--
frames below the body: 1
end
