--TEST--
A fatal error in main's tick, inside a shutdown function, leaves main queued; the request's last pass ends it as a bailout and never pops it again
--FILE--
<?php
/* The shutdown function's zend_try catches the bailout without re-raising it, so main stays in the
 * run queue when the request's last pass finishes it. */
register_shutdown_function(function () {
    TrueAsync\Test\defer('a', null, function () {
        eval('function redeclared() {}');
        eval('function redeclared() {}');
    });
    Async\suspend();
    echo "not reached\n";
});
echo "main\n";
?>
--EXPECTF--
main
microtask a sched=1

Fatal error: Cannot redeclare function redeclared() (previously declared in %s : eval()'d code:1) in %s : eval()'d code on line 1
