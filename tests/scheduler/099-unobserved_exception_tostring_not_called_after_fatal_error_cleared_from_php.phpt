--TEST--
After a fatal error the class's __toString() of an unobserved exception is not called, even when a shutdown function records another error and clears it
--FILE--
<?php
class Loud extends Exception {
    public function __toString(): string {
        echo "__toString ran\n";
        return "Loud";
    }
}

register_shutdown_function(function () {
    $value = @$undefined;
    error_clear_last();
    echo "shutdown function\n";
});

$coroutines = [Async\spawn(function () {
    throw new Loud("lost");
})];
Async\suspend();
eval('function redeclared() {}');
eval('function redeclared() {}');
?>
--EXPECTF--
Fatal error: Cannot redeclare function redeclared() (previously declared in %s : eval()'d code:1) in %s : eval()'d code on line 1
shutdown function

Fatal error: Uncaught Loud: lost in %s:%d
Stack trace:
#0 [internal function]: {closure:%s}()
#1 {main}
  thrown in %s on line %d
