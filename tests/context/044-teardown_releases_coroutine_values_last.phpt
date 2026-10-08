--TEST--
Context: an argument of a coroutine spawned while the request prints an uncaught exception, whose destructor throws, is released after the rest of the teardown ran
--FILE--
<?php

use function Async\spawn;

class Boom
{
    public function __destruct()
    {
        echo "boom destructor\n";
        throw new Exception("from the destructor");
    }
}

class Printed extends Exception
{
    public function __toString(): string
    {
        spawn(fn(Boom $boom) => null, new Boom());
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
%A
teardown: done
boom destructor

Fatal error: Uncaught Exception: from the destructor in %s:%d
Stack trace:
#0 [internal function]: Boom->__destruct()
#1 {main}
  thrown in %s on line %d
