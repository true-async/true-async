--TEST--
A stack that cannot be taken in a shutdown function's suspend() ends the request once, through the scheduler
--FILE--
<?php
register_shutdown_function(function () {
    echo "shutdown\n";
    ini_set('fiber.stack_size', '1');
    $coroutine = Async\spawn(fn() => print("never\n"));
    Async\suspend();
    echo "not reached\n";
});

echo "end\n";
?>
--EXPECTF--
end
shutdown

Fatal error: Uncaught Exception: Fiber stack size is too small, it needs to be at least %d bytes in %s:%d
Stack trace:
#0 %s(%d): Async\suspend()
#1 [internal function]: {closure:%s:%d}()
#2 {main}
  thrown in %s on line %d
