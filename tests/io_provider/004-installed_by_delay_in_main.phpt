--TEST--
The provider is installed when the reactor's queue is created: delay() in a script that never spawns
--FILE--
<?php
var_dump(Io\Hooks\is_active());
Async\delay(1);
var_dump(Io\Hooks\is_active());
?>
--EXPECT--
bool(false)
bool(true)
