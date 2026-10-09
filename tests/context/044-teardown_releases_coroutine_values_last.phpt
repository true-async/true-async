--TEST--
Context: the argument of a coroutine spawned by the __toString() of the request's last uncaught print is released when the coroutine ends, before the teardown
--FILE--
<?php

use function Async\spawn;

class Boom
{
    public function __destruct()
    {
        echo "boom destructor\n";
    }
}

class Printed extends Exception
{
    public function __toString(): string
    {
        spawn(function (Boom $boom) {
            echo "coroutine ran\n";
        }, new Boom());

        return "printed";
    }
}

class Starter
{
    public function __destruct()
    {
        TrueAsync\Test\print_at_teardown();
        spawn(fn() => throw new Printed());
    }
}

$starter = new Starter();
echo "end\n";

?>
--EXPECTF--
end

Fatal error: Uncaught printed
  thrown in %s on line %d
coroutine ran
boom destructor
teardown: done
