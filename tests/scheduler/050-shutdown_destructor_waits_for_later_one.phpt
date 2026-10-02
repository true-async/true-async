--TEST--
A shutdown destructor that yields until a later one has run: the switch handlers move the destructor pass to a new coroutine before the tick, so a yield with nobody else queued lets it run
--FILE--
<?php
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
        global $second_done;
        echo "first destructor start\n";

        while (!$second_done) {
            suspend();
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
second destructor
first destructor end
