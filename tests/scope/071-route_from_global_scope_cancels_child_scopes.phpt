--TEST--
Scope: an error of a coroutine of the global scope cancels its child scopes with a fresh cancellation, safely
--DESCRIPTION--
TrueAsync gives a Scope::inherit() made before any spawn no parent, so there the route never reaches
$child (dev/plans/S9-scope.md 9, item 7).
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\await;
use function Async\delay;

$child = Scope::inherit();
$started = $child->spawn(function () {
    try {
        delay(50);
        echo "started member woke\n";
    } catch (Throwable $e) {
        echo "started member got ", get_class($e), "\n";
    }
});

$unstarted = null;
$failing = spawn(function () use ($child, &$unstarted) {
    $unstarted = $child->spawn(function () {
        echo "unstarted member ran\n";
    });
    throw new RuntimeException("global boom");
});

delay(100);

echo "child cancelled: ", var_export($child->isCancelled(), true), "\n";

try {
    await($unstarted);
} catch (Throwable $e) {
    echo "unstarted: ", get_class($e), ": ", $e->getMessage(), "\n";
}

try {
    await($failing);
} catch (RuntimeException $e) {
    echo "failing: ", $e->getMessage(), "\n";
}

?>
--EXPECT--
started member woke
child cancelled: true
unstarted: Async\AsyncCancellation: Scope was cancelled
failing: global boom
