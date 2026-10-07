--TEST--
A scope disposed while a fatal error unwinds the coroutines drops its finally handlers unrun
--SKIPIF--
<?php
if (getenv("USE_ZEND_ALLOC") === "0") {
    die("skip Zend MM disabled");
}
?>
--INI--
memory_limit=2M
--FILE--
<?php

use Async\Scope;

register_shutdown_function(function () {
    echo "shutdown\n";
});

$scope = Scope::inherit();
$scope->finally(function () {
    echo "not reached\n";
});
$scope->spawn(function () {
    Async\delay(10);
    str_repeat('x', 10000000);
});
Async\suspend();
$scope->disposeSafely();
Async\delay(50);

?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
shutdown
