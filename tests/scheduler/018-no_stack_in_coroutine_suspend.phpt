--TEST--
A stack that cannot be taken in a coroutine's suspend() ends the request once: the next coroutine finishes unrun
--FILE--
<?php
register_shutdown_function(function () use (&$first, &$second) {
    echo "shutdown\n";
    var_dump($first->isCompleted(), $second->isStarted(), $second->isCompleted());
});

$first = Async\spawn(function () use (&$second) {
    echo "first\n";
    ini_set('fiber.stack_size', '64G'); // mmap refuses it (vm.overcommit_memory 0 or 2)
    $second = Async\spawn(fn() => print("never\n"));
    Async\suspend();
    echo "not reached\n";
});

echo "end\n";
?>
--EXPECTF--
end
first

Fatal error: Uncaught Exception: Fiber stack allocate failed: %s in %s:%d
Stack trace:
#0 %s(%d): Async\suspend()
#1 [internal function]: {closure:%s:%d}()
#2 {main}
  thrown in %s on line %d
shutdown
bool(true)
bool(false)
bool(true)
