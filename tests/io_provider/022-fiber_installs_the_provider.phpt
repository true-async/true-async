--TEST--
A Fiber started in a script that uses no coroutine installs the IO provider: every Fiber runs as a coroutine
--FILE--
<?php
var_dump(Io\Hooks\is_active());

$fiber = new Fiber(function () {
    var_dump(Io\Hooks\is_active());
});
$fiber->start();
?>
--EXPECT--
bool(false)
bool(true)
