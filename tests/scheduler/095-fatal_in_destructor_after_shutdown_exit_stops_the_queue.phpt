--TEST--
After exit() in a shutdown function, a fatal error in a destructor still ends the queued coroutines
--FILE--
<?php
class Fails {
    public function __destruct() {
        Async\spawn(function () {
            echo "coroutine ran\n";
        });
        eval('function redeclared() {}');
        eval('function redeclared() {}');
    }
}

register_shutdown_function(function () {
    echo "shutdown function: exit\n";
    exit(0);
});

$fails = new Fails();
echo "main\n";
?>
--EXPECTF--
main
shutdown function: exit

Fatal error: Cannot redeclare function redeclared() (previously declared in %s : eval()'d code:1) in %s : eval()'d code on line 1
