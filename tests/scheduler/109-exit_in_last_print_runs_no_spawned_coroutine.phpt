--TEST--
exit() in the __toString() of an exception printed as uncaught at the request's end: the coroutine that __toString() spawned does not run
--FILE--
<?php

use function Async\spawn;

class Printed extends Exception
{
    public function __toString(): string
    {
        spawn(function () {
            echo "spawned coroutine ran\n";
        });

        echo "exit\n";
        exit(3);
    }
}

class Starter
{
    public function __destruct()
    {
        spawn(fn() => throw new Printed());
    }
}

$starter = new Starter();
echo "end\n";

?>
--EXPECT--
end
exit
