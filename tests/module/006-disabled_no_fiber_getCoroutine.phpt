--TEST--
true_async.enable=0: Fiber has no getCoroutine()
--INI--
true_async.enable=0
--FILE--
<?php
var_dump(method_exists('Fiber', 'getCoroutine'));
?>
--EXPECT--
bool(false)
