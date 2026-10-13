--TEST--
runtime_stats(): fiber_pool_virtual_bytes is each pooled context's C stack and VM stack
--FILE--
<?php

Async\await(Async\spawn(fn() => null));

$stats = Async\runtime_stats();
var_dump($stats['fiber_pool_count'] > 0);
var_dump($stats['fiber_pool_virtual_bytes'] === $stats['fiber_pool_count'] * ($stats['fiber_stack_size'] + 1024 * 16));
var_dump($stats['fiber_pool_capacity'] >= $stats['fiber_pool_count'], $stats['resumed_queue']);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
int(0)
