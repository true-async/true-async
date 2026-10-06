--TEST--
delay(): a negative delay throws ValueError; scheduler context and a finished coroutine releasing its result refuse it
--FILE--
<?php
use function Async\{spawn, delay};
use TrueAsync\Test;

try {
    delay(-1);
} catch (ValueError $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

class OnRelease
{
    public function __destruct()
    {
        try {
            delay(10);
        } catch (Error $e) {
            echo "release: ", $e->getMessage(), "\n";
        }
    }
}

spawn(fn() => new OnRelease());

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
ValueError: Async\delay(): Argument #1 ($ms) must be greater than or equal to 0
microtask m sched=1
Error: The operation cannot be executed in the scheduler context
released m
release: Cannot switch coroutines in the current execution context
end
