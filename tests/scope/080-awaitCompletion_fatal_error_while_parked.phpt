--TEST--
Scope: a fatal error in another coroutine while main is parked in awaitCompletion() ends the request
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
use function Async\spawn;
use function Async\delay;
use function Async\timeout;

register_shutdown_function(function () {
    echo "shutdown\n";
});

$scope = Scope::inherit();
$scope->spawn(function () {
    delay(1000);
    echo "member not reached\n";
});

spawn(function () {
    delay(10);
    str_repeat('x', 10000000);
});

$scope->awaitCompletion(timeout(2000));
echo "not reached\n";

?>
--EXPECTF--

Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
shutdown
