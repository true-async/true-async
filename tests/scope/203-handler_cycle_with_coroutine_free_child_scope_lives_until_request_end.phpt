--TEST--
Scope: a scope object in a cycle with the handler that holds its coroutine-free child scope is not collected by the GC
--FILE--
<?php

use Async\Scope;

// The GC cannot see that only the parent's handler holds the child scope's object, so the cycle lives
// until the request ends.
$scope = new Scope();
$child_scope = Scope::inherit($scope);
$scope->setExceptionHandler(function (Scope $s, Async\Coroutine $c, Throwable $e) use ($scope, $child_scope) {});
$weak_scope = WeakReference::create($scope);
unset($scope, $child_scope);
gc_collect_cycles();
echo "child scope without coroutines, alive: ", var_export($weak_scope->get() !== null, true), "\n";

?>
--EXPECT--
child scope without coroutines, alive: true
