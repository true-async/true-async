--TEST--
true_async.enable=0: the module is loaded and registers no classes, no functions and no scheduler
--INI--
true_async.enable=0
--FILE--
<?php
var_dump(extension_loaded('true_async'));
var_dump(ini_get('true_async.enable'));
var_dump(interface_exists('Async\Awaitable'), class_exists('Async\Coroutine'));
var_dump(function_exists('Async\spawn'), function_exists('TrueAsync\Test\defer'));
?>
--EXPECT--
bool(true)
string(1) "0"
bool(false)
bool(false)
bool(false)
bool(false)
