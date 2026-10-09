--TEST--
After a fatal error in the script, an exception left uncaught by a shutdown function's coroutine is printed by the built-in __toString(), not its class's
--FILE--
<?php

class Printed extends Exception
{
    public function __toString(): string
    {
        echo "the class's __toString() ran\n";

        return "printed";
    }
}

function declared_twice() {}

register_shutdown_function(fn() => Async\spawn(fn() => throw new Printed("from the shutdown function's coroutine")));

if (true) {
    function declared_twice() {}
}

?>
--EXPECTF--
Fatal error: Cannot redeclare function declared_twice() (previously declared in %s:%d) in %s on line %d

Fatal error: Uncaught Printed: from the shutdown function's coroutine in %s:%d
Stack trace:
#0 [internal function]: {closure:%s}()
#1 {main}
  thrown in %s on line %d
