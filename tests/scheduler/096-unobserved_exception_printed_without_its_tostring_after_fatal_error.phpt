--TEST--
After a fatal error an exception nobody observed is printed by the built-in __toString(): its class's own does not run
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
echo "main\n";
eval('function redeclared() {}');
eval('function redeclared() {}');
?>
--EXPECTF--
main

Fatal error: Cannot redeclare function redeclared() (previously declared in %s : eval()'d code:1) in %s : eval()'d code on line 1

Fatal error: Uncaught Loud: lost in %s:%d
Stack trace:
#0 [internal function]: {closure:%s}()
#1 {main}
  thrown in %s on line %d
