--TEST--
await_all() and Future::await() in a destructor that a finished coroutine's release runs are refused
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await_all;

class Waits
{
    public function __destruct()
    {
        $completed = Future::completed(1);
        $completed->ignore();

        try {
            await_all([$completed]);
        } catch (Throwable $e) {
            echo get_class($e), ": ", $e->getMessage(), "\n";
        }

        $state = new FutureState();
        $state->ignore();

        try {
            (new Future($state))->await();
        } catch (Throwable $e) {
            echo get_class($e), ": ", $e->getMessage(), "\n";
        }
    }
}

spawn(function () {
    return new Waits();
});

Async\suspend();
echo "end\n";
?>
--EXPECT--
Async\AsyncException: Cannot await futures outside of a coroutine
Error: await() requires a running coroutine
end
