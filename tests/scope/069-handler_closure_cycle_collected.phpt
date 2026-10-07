--TEST--
Scope: a handler whose closure holds its scope does not keep the scope alive once nothing else does
--FILE--
<?php

use Async\Scope;

// Without the handlers in the object's get_gc the cycle object -> scope -> handler -> object would
// stay until the request ends.
gc_collect_cycles();
$before = memory_get_usage();

for ($i = 0; $i < 1000; $i++) {
    $scope = new Scope();
    $scope->setExceptionHandler(function (Scope $s, Async\Coroutine $c, Throwable $e) use ($scope) {});
    $scope->setChildScopeExceptionHandler(function (Scope $s, Async\Coroutine $c, Throwable $e) use ($scope) {});
}

unset($scope);
// One per scope: the object with its closures.
echo "collected: ", gc_collect_cycles(), "\n";
// The thousand cycles take more than a megabyte; the collector's own buffer stays.
echo "memory back: ", var_export(memory_get_usage() - $before < 100000, true), "\n";

?>
--EXPECT--
collected: 1000
memory back: true
