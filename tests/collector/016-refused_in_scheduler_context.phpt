--TEST--
get_deadlocked_coroutines(): refused in scheduler context, here a microtask
--FILE--
<?php
use TrueAsync\Test;

Test\defer('a', null, function () {
    try {
        Async\get_deadlocked_coroutines();
    } catch (Error $error) {
        echo $error->getMessage(), "\n";
    }
});

Async\suspend();
echo "end\n";
?>
--EXPECT--
microtask a sched=1
The operation cannot be executed in the scheduler context
released a
end
