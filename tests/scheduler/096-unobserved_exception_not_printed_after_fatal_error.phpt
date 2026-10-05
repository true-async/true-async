--TEST--
After a fatal error an exception nobody observed is not printed: its __toString() would run after the fatal error
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
