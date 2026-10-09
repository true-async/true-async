--TEST--
A resume() whose caller is cancelled after the fiber yielded leaves the yielded value to nobody: the fiber can be resumed again and the value is released
--FILE--
<?php
use function Async\spawn;

$fiber = new Fiber(function () {
    Fiber::suspend();
    $value = Fiber::suspend(new stdClass);
    echo "body got $value\n";
    Fiber::suspend();
});
$fiber->start();

$waiter = spawn(function () use ($fiber) {
    $fiber->resume();
    echo "not reached\n";
});

Async\suspend();
$waiter->cancel();
Async\suspend();

$fiber->resume('from main');
echo "end\n";
?>
--EXPECT--
body got from main
end
