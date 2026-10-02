--TEST--
Fiber::getCurrent() is the fiber inside its body only: a coroutine spawned there, main and a coroutine started on a fresh context see null, before and after a park
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;

$fiber = new Fiber(function () {
    var_dump(Fiber::getCurrent() !== null);

    spawn(function () {
        var_dump(Fiber::getCurrent());
        suspend();
        var_dump(Fiber::getCurrent());
    });

    suspend();
    var_dump(Fiber::getCurrent() !== null);
});

$fiber->start();
unset($fiber);
var_dump(Fiber::getCurrent());
echo "main end\n";
?>
--EXPECT--
bool(true)
NULL
bool(true)
NULL
NULL
main end
