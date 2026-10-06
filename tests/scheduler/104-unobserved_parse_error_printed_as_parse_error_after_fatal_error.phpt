--TEST--
After a fatal error a ParseError nobody observed is printed as the engine prints an uncaught one
--FILE--
<?php
$coroutines = [Async\spawn(function () {
    eval('this is not php');
})];
Async\suspend();
echo "main\n";
eval('function redeclared() {}');
eval('function redeclared() {}');
?>
--EXPECTF--
main

Fatal error: Cannot redeclare function redeclared() (previously declared in %s : eval()'d code:1) in %s : eval()'d code on line 1

Parse error: syntax error, unexpected identifier "is" in %s : eval()'d code on line 1
