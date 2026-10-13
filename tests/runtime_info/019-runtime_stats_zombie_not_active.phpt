--TEST--
runtime_stats(): a zombie coroutine (left running by disposeSafely()) is counted in coroutines_total, not in coroutines_active
--FILE--
<?php

use Async\Scope;
use function Async\suspend;

$scope = new Scope();
$scope->spawn(function () {
    suspend();
    suspend();
});
suspend();
$scope->disposeSafely();

$stats = Async\runtime_stats();
var_dump($stats['coroutines_total'] - $stats['coroutines_active']);
?>
--EXPECT--
int(1)
