--TEST--
TaskGroup: the destructor's cancel of what remains in its external scope, with a GC run due inside the walk, frees no coroutine or scope the walk reads next
--FILE--
<?php

use Async\Scope;
use Async\TaskGroup;

// The scope's own coroutine is cancelled twice, by the scope's cancel and by the walk over what remains; the second
// releases its cancellation. One root short of the core's wait for a queued collection (100 000 roots past the
// threshold) puts that release inside the walk, where a wait for the collection would let the coroutine finish and
// free the scope before the walk reads it again.
$scope = new Scope();
$group = new TaskGroup(scope: $scope);
$group->spawn(fn() => 1);
$scope->spawn(function () {
    Async\suspend();
    Async\suspend();
    echo "scope's coroutine ends\n";
});

Async\suspend();
Async\suspend();

$held = [];
$status = gc_status();

for ($i = $status['threshold'] - $status['roots'] - 1 + 100000; $i > 0; $i--) {
    $object = new stdClass();
    $held[] = $object;
    unset($object);
}

$runs = gc_status()['runs'];
$group = null;

Async\suspend();
echo "collection after: ", gc_status()['runs'] > $runs ? "yes" : "no", "\n";
var_dump($scope->isFinished());
?>
--EXPECT--
collection after: yes
bool(true)
