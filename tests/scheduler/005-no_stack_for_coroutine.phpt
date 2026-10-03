--TEST--
A coroutine that cannot get a stack ends the request; it and the queued ones finish unrun
--FILE--
<?php
register_shutdown_function(function () use (&$first, &$second) {
    echo "shutdown\n";
    var_dump($first->isStarted(), $first->isCompleted(), $second->isStarted(), $second->isCompleted());
    var_dump(count(Async\get_coroutines()));
});

$first = Async\spawn(fn() => print("first ran\n"));
$second = Async\spawn(fn() => print("second ran\n"));
ini_set('fiber.stack_size', '64G'); // mmap refuses it (vm.overcommit_memory 0 or 2)
echo "end\n";
?>
--EXPECTF--
end

Fatal error: Uncaught Exception: Fiber stack allocate failed: %s in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
shutdown
bool(false)
bool(true)
bool(false)
bool(true)
int(1)
