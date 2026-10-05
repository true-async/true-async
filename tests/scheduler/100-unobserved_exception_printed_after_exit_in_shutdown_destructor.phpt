--TEST--
An exit() in a shutdown destructor does not drop an exception nobody observed: it is printed, without the class's __toString()
--FILE--
<?php
class Loud extends Exception {
    public function __toString(): string {
        echo "__toString ran\n";
        return "Loud";
    }
}

class Leaves {
    public function __destruct() {
        echo "destructor: exit\n";
        exit(0);
    }
}

$coroutines = [Async\spawn(function () {
    throw new Loud("lost");
})];
Async\suspend();
$leaves = new Leaves();
echo "main\n";
?>
--EXPECTF--
main
destructor: exit

Fatal error: Uncaught Loud: lost in %s:%d
Stack trace:
#0 [internal function]: {closure:%s}()
#1 {main}
  thrown in %s on line %d
