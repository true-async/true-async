--TEST--
CpuSnapshot: a clone is an equal snapshot
--FILE--
<?php

$snapshot = Async\CpuSnapshot::now();
$copy = clone $snapshot;
var_dump($copy == $snapshot, $copy !== $snapshot);
?>
--EXPECT--
bool(true)
bool(true)
