--TEST--
delay(): scheduler context refuses it
--FILE--
<?php
use function Async\delay;
use TrueAsync\Test;

Test\defer('m', null, function () {
    try {
        delay(10);
    } catch (Error $e) {
        echo get_class($e), ": ", $e->getMessage(), "\n";
    }
});

Async\suspend();
echo "end\n";
?>
--EXPECT--
microtask m sched=1
Error: The operation cannot be executed in the scheduler context
released m
end
