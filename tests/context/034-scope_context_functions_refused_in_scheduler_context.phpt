--TEST--
Context: current_context(), root_context() and request_context() refuse in scheduler context (a microtask)
--FILE--
<?php

use TrueAsync\Test;
use function Async\await;
use function Async\spawn;

$target = spawn(fn() => "target");

Test\defer('A', null, function () {
    foreach (['current_context', 'root_context', 'request_context'] as $function) {
        try {
            ("Async\\$function")();
            echo "$function: no throw\n";
        } catch (Error $e) {
            echo "$function: ", $e->getMessage(), "\n";
        }
    }
});

await($target);

?>
--EXPECT--
microtask A sched=1
current_context: The operation cannot be executed in the scheduler context
root_context: The operation cannot be executed in the scheduler context
request_context: The operation cannot be executed in the scheduler context
released A
