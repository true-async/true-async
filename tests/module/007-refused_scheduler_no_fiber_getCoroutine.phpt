--TEST--
The core refuses the scheduler when another extension registered one first: Fiber has no getCoroutine()
--SKIPIF--
<?php if (!extension_loaded('test_scheduler')) die('skip the core is built without ext/test_scheduler'); ?>
--INI--
test_scheduler.enable=1
--FILE--
<?php
var_dump(method_exists('Fiber', 'getCoroutine'));
?>
--EXPECT--
Warning: The module true_async cannot register an Async scheduler: test_scheduler already did in Unknown on line 0
bool(false)
