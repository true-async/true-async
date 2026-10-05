--TEST--
An uncaught exception in main is not a fatal error for the end of the request: an exception nobody observed is still printed after it
--FILE--
<?php
class Loud extends Exception {
    public function __toString(): string {
        echo "__toString ran\n";
        return "Loud";
    }
}

$coroutines = [Async\spawn(function () {
    throw new Loud("lost");
})];
Async\suspend();
throw new LogicException("main");
?>
--EXPECTF--
Fatal error: Uncaught LogicException: main in %s:%d
Stack trace:
#0 {main}
  thrown in %s on line %d
__toString ran

Fatal error: Uncaught Loud
  thrown in %s on line %d
