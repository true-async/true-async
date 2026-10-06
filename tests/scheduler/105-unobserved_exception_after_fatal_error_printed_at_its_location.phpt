--TEST--
After a fatal error an exception nobody observed is printed at the file and line it was thrown from
--FILE--
<?php
$coroutines = [Async\spawn(function () {
    throw new Exception("lost");
})];
Async\suspend();
eval('function redeclared() {}');
eval('function redeclared() {}');
?>
--EXPECTF--
Fatal error: Cannot redeclare function redeclared() (previously declared in %s : eval()'d code:1) in %s : eval()'d code on line 1

Fatal error: Uncaught Exception: lost in %s:3
Stack trace:
#0 [internal function]: {closure:%s}()
#1 {main}
  thrown in %s%e105-unobserved_exception_after_fatal_error_printed_at_its_location.php on line 3
