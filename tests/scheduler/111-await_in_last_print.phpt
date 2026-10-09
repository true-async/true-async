--TEST--
The __toString() of an exception printed as uncaught at the request's end awaits a coroutine it spawned
--FILE--
<?php

use function Async\await;
use function Async\spawn;

class Printed extends Exception
{
    public function __toString(): string
    {
        return await(spawn(fn() => "awaited text"));
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

Fatal error: Uncaught awaited text
  thrown in %s on line %d
