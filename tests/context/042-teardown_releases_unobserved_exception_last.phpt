--TEST--
Context: an unobserved exception of a coroutine spawned from a shutdown destructor, whose destructor throws, is released after the rest of the teardown ran
--FILE--
<?php

use function Async\spawn;

class Boom extends Exception
{
    public function __destruct()
    {
        echo "boom destructor\n";
        throw new Exception("from the destructor");
    }
}

class Starter
{
    public function __destruct()
    {
        TrueAsync\Test\print_at_teardown();
        spawn(fn() => throw new Boom("unobserved"));
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
