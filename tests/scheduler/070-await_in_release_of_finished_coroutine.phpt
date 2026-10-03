--TEST--
await() in a destructor that runs while a finished coroutine releases its cancellation is refused: the finished coroutine is still current and cannot wait
--FILE--
<?php
use function Async\spawn;
use function Async\await;

class AwaitsOnRelease extends Async\AsyncCancellation
{
    public function __destruct()
    {
        try {
            await(spawn(fn() => 1));
            echo "destructor: awaited\n";
        } catch (Error $e) {
            echo "destructor: ", $e->getMessage(), "\n";
        }
    }
}

$coroutine = spawn(function () {
    echo "body\n";
});
$coroutine->cancel(new AwaitsOnRelease("cancelled"));
unset($coroutine);
echo "main end\n";
?>
--EXPECT--
main end
destructor: await() requires a running coroutine
