--TEST--
A Timeout never cancelled is collected in a garbage cycle
--FILE--
<?php
$cycle = new stdClass();
$cycle->self = $cycle;
$cycle->timeout = Async\timeout(1000);
unset($cycle);
var_dump(gc_collect_cycles());
?>
--EXPECT--
int(2)
