--TEST--
A fatal error in a shutdown destructor ends the coroutines queued before it: they would run with every object marked destructed
--FILE--
<?php
class Guard {
    public function __destruct() {
        echo "guard released\n";
    }
}

class Fails {
    public function __destruct() {
        $guard = new Guard();
        Async\spawn(function () use (&$guard) {
            echo "coroutine ran\n";
            $guard = null;
        });
        eval('function redeclared() {}');
        eval('function redeclared() {}');
    }
}

$fails = new Fails();
echo "main\n";
?>
--EXPECTF--
main

Fatal error: Cannot redeclare function redeclared() (previously declared in %s : eval()'d code:1) in %s : eval()'d code on line 1
