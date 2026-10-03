--TEST--
A finish handler that clears the coroutine's exception marks it handled: nobody awaits the coroutine, and the request does not end with the exception
--FILE--
<?php
use function Async\spawn;

$coroutine = spawn(function () {
    throw new RuntimeException("taken");
});
TrueAsync\Test\add_clearing_finish_handler($coroutine);
unset($coroutine);

echo "main end\n";
?>
--EXPECT--
main end
finish handler takes RuntimeException
