--TEST--
Context: coroutine_context() refuses in scheduler context (a microtask)
--FILE--
<?php

use TrueAsync\Test;
use function Async\await;
use function Async\spawn;

$target = spawn(fn() => "target");

Test\defer('A', null, function () {
    try {
        Async\coroutine_context();
        echo "no throw\n";
    } catch (Error $e) {
        echo $e->getMessage(), "\n";
    }
});

await($target);

?>
--EXPECT--
microtask A sched=1
The operation cannot be executed in the scheduler context
released A
