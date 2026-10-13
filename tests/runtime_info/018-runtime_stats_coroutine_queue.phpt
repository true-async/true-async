--TEST--
runtime_stats(): three spawned coroutines that have not run yet are in coroutine_queue
--FILE--
<?php

use function Async\spawn;

spawn(fn() => null);
spawn(fn() => null);
spawn(fn() => null);

$stats = Async\runtime_stats();
var_dump($stats['coroutine_queue'], $stats['coroutines_total']);
?>
--EXPECT--
int(3)
int(4)
