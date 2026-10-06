--TEST--
The provider is installed when the first coroutine other than main is spawned, not before
--FILE--
<?php
var_dump(Io\Hooks\is_active());
Async\spawn(function () { echo "spawned\n"; });
var_dump(Io\Hooks\is_active());
?>
--EXPECT--
bool(false)
bool(true)
spawned
