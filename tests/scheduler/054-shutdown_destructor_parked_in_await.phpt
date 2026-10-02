--TEST--
A shutdown destructor parked in await() on a coroutine that waits for a later destructor: the pass goes on in the core's new coroutine while it waits
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$second_done = false;

final class Second
{
    public function __destruct()
    {
        global $second_done;
        echo "second destructor\n";
        $second_done = true;
    }
}

final class First
{
    public function __destruct()
    {
        echo "first destructor start\n";

        await(spawn(function () {
            global $second_done;

            while (!$second_done) {
                suspend();
            }
        }));

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
second destructor
first destructor end
