--TEST--
Scope: a fatal error in the handler of a scope with an object ends the request, and the coroutine leaves its scope
--SKIPIF--
<?php
$zend_mm_enabled = getenv("USE_ZEND_ALLOC");
if ($zend_mm_enabled === "0") {
    die("skip Zend MM disabled");
}
?>
--INI--
memory_limit=2M
--FILE--
<?php

use Async\Scope;
use function Async\delay;

register_shutdown_function(function () {
    echo "shutdown\n";
});

$scope = new Scope();
$scope->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) {
    echo "handler: ", $e->getMessage(), "\n";
    str_repeat('x', 10000000);
    echo "not reached\n";
});

$failing = $scope->spawn(function () {
    throw new RuntimeException("boom");
});

delay(10);
echo "not reached\n";

?>
--EXPECTF--
handler: boom

Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
shutdown

Fatal error: Uncaught RuntimeException: boom in %s:%d
Stack trace:
#0 [internal function]: {closure:%s}()
#1 {main}
  thrown in %s on line %d
