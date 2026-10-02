--TEST--
A coroutine that cannot get a stack ends the request even with a user exception handler, as running out of memory does
--FILE--
<?php
set_exception_handler(function (Throwable $e) {
    echo "handler: ", get_class($e), "\n";
});
register_shutdown_function(function () use (&$first, &$second) {
    echo "shutdown\n";
    var_dump($first->isStarted(), $first->isCompleted(), $second->isStarted(), $second->isCompleted());
    var_dump(count(Async\get_coroutines()));
});
$first = Async\spawn(fn() => print("first ran\n"));
$second = Async\spawn(fn() => print("second ran\n"));
ini_set('fiber.stack_size', '1');
echo "end\n";
?>
--EXPECTF--
end

Fatal error: Uncaught Exception: Fiber stack size is too small, it needs to be at least %d bytes in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
shutdown
bool(false)
bool(true)
bool(false)
bool(true)
int(1)
