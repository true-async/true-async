--TEST--
A stack that cannot be taken in a shutdown function's suspend() ends the request once, through the scheduler
--FILE--
<?php
register_shutdown_function(function () {
    echo "shutdown\n";
    $coroutine = Async\spawn(fn() => print("never\n"));
    // After the spawn, which created the scheduler with its own stack; 1 PiB is more than mmap
    // or VirtualAlloc places without an address hint, so every system refuses it.
    ini_set('fiber.stack_size', '1048576G');
    Async\suspend();
    echo "not reached\n";
});

echo "end\n";
?>
--EXPECTF--
end
shutdown

Fatal error: Uncaught Exception: Fiber stack allocate failed: %s in %s:%d
Stack trace:
#0 %s(%d): Async\suspend()
#1 [internal function]: {closure:%s:%d}()
#2 {main}
  thrown in %s on line %d
