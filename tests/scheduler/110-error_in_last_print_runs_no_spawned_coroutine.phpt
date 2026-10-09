--TEST--
An Error thrown by the __toString() of an exception printed as uncaught at the request's end: the coroutine that __toString() spawned does not run
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

        throw new Error("from __toString");
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
--EXPECTF--
end

Fatal error: Uncaught Error: from __toString in %s:%d
Stack trace:
#0 [internal function]: Printed->__toString()
#1 {main}
  thrown in %s on line %d
