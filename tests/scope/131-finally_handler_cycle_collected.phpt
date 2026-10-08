--TEST--
Scope: a scope object in a cycle with its finally handler alone, with no coroutine, is collected by gc_collect_cycles() and the handler runs
--FILE--
<?php

use Async\Scope;
use function Async\suspend;

$scope = new Scope();
$scope->finally(function () use ($scope) {
    echo "finally handler\n";
});
$weak_scope = WeakReference::create($scope);
unset($scope);

gc_collect_cycles();
// The destructor the GC called started the finally run, whose closure holds the object until it ends.
suspend();
gc_collect_cycles();
echo "alive: ", var_export($weak_scope->get() !== null, true), "\n";

?>
--EXPECT--
finally handler
alive: false
