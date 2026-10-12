--TEST--
Scope: disposeSafely() with a GC run due inside its walk, where it makes a zombie, frees no scope the walk reads next
--FILE--
<?php

use Async\Scope;

// The started coroutine becomes a zombie and the walk releases the cancellation it would have got. One root short
// of the threshold puts that release inside the walk, where a wait for the collection would let the zombie finish
// and free the scope before the walk reads it again.
$scope = new Scope();
$scope->spawn(function () {
    Async\suspend();
    echo "zombie ends\n";
});

Async\suspend();

$held = [];
$status = gc_status();

for ($i = $status['threshold'] - $status['roots'] - 1; $i > 0; $i--) {
    $object = new stdClass();
    $held[] = $object;
    unset($object);
}

$runs = gc_status()['runs'];
$scope->disposeSafely();

Async\suspend();
echo "collection after: ", gc_status()['runs'] > $runs ? "yes" : "no", "\n";
var_dump($scope->isFinished());
?>
--EXPECT--
zombie ends
collection after: yes
bool(true)
