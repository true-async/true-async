--TEST--
A refused scheduler leaves out Future, FutureState and Timeout: a Future's callbacks run in coroutines that need it
--SKIPIF--
<?php if (!extension_loaded('test_scheduler')) die('skip the core is built without ext/test_scheduler'); ?>
--INI--
test_scheduler.enable=1
--FILE--
<?php
var_dump(class_exists('Async\Future'), class_exists('Async\FutureState'), class_exists('Async\Timeout'));
?>
--EXPECT--
Warning: The module true_async cannot register an Async scheduler: test_scheduler already did in Unknown on line 0
bool(false)
bool(false)
bool(false)
