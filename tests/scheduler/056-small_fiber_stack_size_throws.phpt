--TEST--
A coroutine on a fiber.stack_size too small for PHP code ends with the core's stack-limit Error, as a Fiber of that size does, not a crash
--FILE--
<?php
ini_set('fiber.stack_size', '16K');

try {
    var_dump(Async\await(Async\spawn(fn() => 1)));
} catch (Error $error) {
    echo get_class($error), ": ", $error->getMessage(), "\n";
}

try {
    $fiber = new Fiber(fn() => 1);
    $fiber->start();
} catch (Error $error) {
    echo get_class($error), ": ", $error->getMessage(), "\n";
}
?>
--EXPECTF--
Error: Maximum call stack size of %d bytes %s reached. Infinite recursion?
Error: Maximum call stack size of %d bytes %s reached. Infinite recursion?
