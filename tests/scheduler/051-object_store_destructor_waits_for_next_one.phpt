--TEST--
The object store's shutdown pass: a destructor that waits for the next object's destructor lets the pass go on in a new coroutine
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

/* Held by an array, not by a variable: the object store's pass, in creation order. */
$objects = [new First(), new Second()];
$objects[] = $objects;

echo "main end\n";
?>
--EXPECT--
main end
first destructor start
second destructor
first destructor end
