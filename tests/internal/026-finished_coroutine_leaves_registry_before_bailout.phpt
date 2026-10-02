--TEST--
A coroutine whose finish bailed out has left the registry: a drain later in the request ends instead of counting it as a waiter forever
--FILE--
<?php
use function Async\spawn;
use function Async\current_coroutine;
use TrueAsync\Test;

spawn(function () {
    echo "coroutine bails out in its finish\n";
    Test\add_throwing_finish_handler(current_coroutine(), true);
});

register_shutdown_function(function () {
    spawn(fn() => print("shutdown coroutine\n"));
});

echo "main end\n";
?>
--EXPECTF--
main end
coroutine bails out in its finish

Fatal error: finish handler of coroutine %d bails out in Unknown on line 0
shutdown coroutine
