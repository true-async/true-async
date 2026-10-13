--TEST--
Scope: a scope object in a cycle with its handlers survives the GC while a coroutine of its child scope runs, and the GC collects it once that coroutine ends
--FILE--
<?php

use Async\Scope;
use function Async\suspend;

$is_released = false;

function start(bool $in_child_scope): array
{
    $scope = new Scope();
    $scope->setExceptionHandler(function (Scope $s, Async\Coroutine $c, Throwable $e) use ($scope) {});
    $scope->finally(function () use ($scope) {});
    // Returned: the child scope object's destructor would cancel the child scope.
    $child_scope = $in_child_scope ? Scope::inherit($scope) : null;

    $member = ($child_scope ?? $scope)->spawn(function () {
        try {
            while (!$GLOBALS['is_released']) {
                suspend();
            }
            echo "member: finished\n";
        } catch (Async\AsyncCancellation $e) {
            echo "member: cancelled\n";
        }
    });
    while (!$member->isStarted()) {
        suspend();
    }

    return [$member, WeakReference::create($scope), $child_scope];
}

$is_released = false;
[$member, $scope, $child_scope] = start(true);
gc_collect_cycles();
echo "alive while the member runs: ", var_export($scope->get() !== null, true), "\n";

$is_released = true;
while (!$member->isCompleted()) {
    suspend();
}
unset($child_scope);
gc_collect_cycles();
// The destructor the GC called started the finally run, whose closure holds the object until it ends.
suspend();
gc_collect_cycles();
echo "alive after: ", var_export($scope->get() !== null, true), "\n";

?>
--EXPECT--
alive while the member runs: true
member: finished
alive after: false
