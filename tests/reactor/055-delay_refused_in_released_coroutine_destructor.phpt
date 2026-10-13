--TEST--
delay(): a finished coroutine releasing its result refuses it
--FILE--
<?php
use function Async\spawn;

class OnRelease
{
    public function __destruct()
    {
        try {
            Async\delay(10);
        } catch (Error $e) {
            echo "release: ", $e->getMessage(), "\n";
        }
    }
}

spawn(fn() => new OnRelease());

Async\suspend();
echo "end\n";
?>
--EXPECT--
release: Cannot switch coroutines in the current execution context
end
