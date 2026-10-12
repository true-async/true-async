--TEST--
Scope: cancel() with a GC run due inside its walk, where it releases a coroutine's earlier cancellation, frees no coroutine or scope the walk reads next
--FILE--
<?php

use Async\Scope;

// The coroutine's own cancellation is pending, so the scope's cancel releases the one it brings. One root short of
// the threshold puts that release inside the walk, where a wait for the collection would let the coroutine finish
// unrun and free itself and the scope before the walk reads them again.
$scope = new Scope();
$scope->spawn(function () {
    echo "not reached\n";
})->cancel();

$held = [];
$status = gc_status();

for ($i = $status['threshold'] - $status['roots'] - 1; $i > 0; $i--) {
    $object = new stdClass();
    $held[] = $object;
    unset($object);
}

$runs = gc_status()['runs'];
$scope->cancel();

Async\suspend();
echo "collection after: ", gc_status()['runs'] > $runs ? "yes" : "no", "\n";
var_dump($scope->isCancelled(), $scope->isFinished());
?>
--EXPECT--
collection after: yes
bool(true)
bool(true)
