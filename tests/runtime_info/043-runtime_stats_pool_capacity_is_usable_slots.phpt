--TEST--
runtime_stats(): fiber_pool_capacity counts the usable slots, one less than the ring's power-of-two size
--FILE--
<?php

Async\await(Async\spawn(fn() => null));

$capacity = Async\runtime_stats()['fiber_pool_capacity'];
var_dump($capacity > 0 && (($capacity + 1) & $capacity) === 0);
?>
--EXPECT--
bool(true)
