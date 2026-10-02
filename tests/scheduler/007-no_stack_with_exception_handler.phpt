--TEST--
With a user exception handler, a coroutine that cannot get a stack finishes unrun
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
--EXPECT--
end
handler: Exception
handler: Exception
shutdown
bool(false)
bool(true)
bool(false)
bool(true)
int(1)
