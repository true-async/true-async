--TEST--
The core refuses the scheduler when another extension registered one first: a warning, the classes stay, no Async\ functions
--SKIPIF--
<?php if (!extension_loaded('test_scheduler')) die('skip the core is built without ext/test_scheduler'); ?>
--INI--
test_scheduler.enable=1
--FILE--
<?php
var_dump(class_exists('Async\Coroutine'));
var_dump(function_exists('Async\spawn'), function_exists('TrueAsync\Test\defer'));
?>
--EXPECT--
Warning: The module true_async cannot register an Async scheduler: test_scheduler already did in Unknown on line 0
bool(true)
bool(false)
bool(false)
