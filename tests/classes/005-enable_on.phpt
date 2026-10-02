--TEST--
true_async.enable takes a boolean word: On enables the extension and registers its classes
--INI--
true_async.enable=On
--FILE--
<?php
var_dump(class_exists(Async\Coroutine::class), interface_exists(Async\Awaitable::class));
?>
--EXPECT--
bool(true)
bool(true)
