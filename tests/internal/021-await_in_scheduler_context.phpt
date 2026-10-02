--TEST--
In scheduler context (a microtask) Async\await() refuses before it waits, and a collection is started without waiting
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use TrueAsync\Test;

$target = spawn(function () {
    suspend();
    return "target";
});

Test\defer('A', null, function () use ($target) {
    try {
        await($target);
    } catch (Error $error) {
        echo "await: ", $error->getMessage(), "\n";
    }

    var_dump(gc_collect_cycles());
});

echo "main: " . await($target) . "\n";
?>
--EXPECT--
microtask A sched=1
await: The operation cannot be executed in the scheduler context
int(0)
released A
main: target
