--TEST--
exit() in a coroutine awaited by a shutdown destructor cancels the destructor's wait, and the pass goes on without the coroutine queued to carry it
--FILE--
<?php
use function Async\spawn;
use function Async\await;

final class Second
{
    public function __destruct()
    {
        echo "second destructor\n";
    }
}

final class First
{
    public function __destruct()
    {
        echo "first destructor start\n";
        try {
            await(spawn(function () {
                echo "coroutine exits\n";
                exit(0);
            }));
        } catch (Throwable $e) {
            echo get_class($e), ": ", $e->getMessage(), "\n";
        }
        echo "first destructor end\n";
    }
}

$second = new Second();
$first = new First();

echo "main end\n";
?>
--EXPECT--
main end
first destructor start
coroutine exits
Async\AsyncCancellation: Graceful shutdown
first destructor end
second destructor
