--TEST--
A coroutine's finally handlers are dropped after a fatal error while its scope's still run
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

$scope = new Scope();
$scope->finally(function () {
    echo "scope finally\n";
});

$scope->spawn(function () {
    Async\current_coroutine()->finally(function () {
        echo "coroutine finally\n";
    });
    str_repeat('x', 10000000);
});

?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
scope finally
