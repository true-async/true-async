--TEST--
D16: the exit deadline bounds the drain, not the request's last print of an uncaught exception, whose __toString() may wait past it
--FILE--
<?php
use function Async\{spawn, delay};
use TrueAsync\Test;

Test\set_exit_deadline(50);

class Printed extends Exception
{
    public function __toString(): string
    {
        delay(200);

        return "printed after the wait";
    }
}

class Starter
{
    public function __destruct()
    {
        // A coroutine that ran and still waits arms D16's deadline for the drain.
        spawn(fn() => delay(10));
        spawn(fn() => throw new Printed());
    }
}

$starter = new Starter();
echo "end\n";
?>
--EXPECTF--
end

Fatal error: Uncaught printed after the wait
  thrown in %s on line %d
