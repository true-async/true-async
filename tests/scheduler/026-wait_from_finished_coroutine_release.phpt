--TEST--
A destructor run when a finished coroutine releases its result cannot wait: await() and suspend() refuse with an Error and the GC collects later
--FILE--
<?php
use function Async\spawn;
use function Async\await;

class OnRelease
{
    public function __destruct()
    {
        try {
            await(spawn(fn() => "x"));
        } catch (Error $error) {
            echo "await: ", $error->getMessage(), "\n";
        }

        try {
            Async\suspend();
        } catch (Error $error) {
            echo "suspend: ", $error->getMessage(), "\n";
        }

        echo "gc: ", gc_collect_cycles(), "\n";
    }
}

spawn(fn() => new OnRelease());
echo "end\n";
?>
--EXPECT--
end
await: await() requires a running coroutine
suspend: Cannot switch coroutines in the current execution context
gc: 0
