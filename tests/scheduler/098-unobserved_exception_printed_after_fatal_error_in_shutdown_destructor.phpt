--TEST--
After a fatal error in a shutdown destructor an exception nobody observed is still printed, by the built-in __toString()
--FILE--
<?php
class Loud extends Exception {
    public function __toString(): string {
        echo "__toString ran\n";
        return "Loud";
    }
}

class Fails {
    public function __destruct() {
        eval('function redeclared() {}');
        eval('function redeclared() {}');
    }
}

$coroutines = [Async\spawn(function () {
    throw new Loud("lost");
})];
Async\suspend();
$fails = new Fails();
echo "main\n";
?>
--EXPECTF--
main

Fatal error: Cannot redeclare function redeclared() (previously declared in %s : eval()'d code:1) in %s : eval()'d code on line 1

Fatal error: Uncaught Loud: lost in %s:%d
Stack trace:
#0 [internal function]: {closure:%s}()
#1 {main}
  thrown in %s on line %d
