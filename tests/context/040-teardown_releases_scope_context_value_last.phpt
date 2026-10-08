--TEST--
Context: a value of the root context set after the shutdown destructors, whose destructor throws, is released after the rest of the teardown ran
--FILE--
<?php

use function Async\root_context;
use function Async\spawn;

class Boom
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
        spawn(fn() => root_context()->set('boom', new Boom()));
    }
}

$starter = new Starter();
echo "end\n";

?>
--EXPECTF--
end
teardown: done
boom destructor

Fatal error: Uncaught Exception: from the destructor in %s:%d
Stack trace:
#0 [internal function]: Boom->__destruct()
#1 {main}
  thrown in %s on line %d
