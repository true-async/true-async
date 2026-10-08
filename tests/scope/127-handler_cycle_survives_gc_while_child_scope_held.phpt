--TEST--
Scope: a scope object in a cycle with its handler survives the GC while the script holds an idle child scope, and an error routed from the child later reaches the handler with the object
--FILE--
<?php

use Async\Scope;
use function Async\suspend;

$parent = new Scope();
$parent->setChildScopeExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) use ($parent) {
    echo "handler: ", $e->getMessage(), ", the parent's own object: ", var_export($scope === $parent, true), "\n";
});
$child = Scope::inherit($parent);
$weak_parent = WeakReference::create($parent);
unset($parent);

gc_collect_cycles();
echo "parent closed: ", var_export($weak_parent->get()?->isClosed(), true), "\n";

$child->spawn(function () {
    throw new Exception("from the child");
});
suspend();
suspend();
echo "end\n";

?>
--EXPECT--
parent closed: false
handler: from the child, the parent's own object: true
end
