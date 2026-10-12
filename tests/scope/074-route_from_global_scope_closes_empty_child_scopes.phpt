--TEST--
Scope: an unhandled error in the global scope closes its child scopes, empty ones too, while the global scope still spawns
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\suspend;
use function Async\await;

$warm = spawn(fn() => null);
await($warm);

$pool = Scope::inherit();
$failing = spawn(function () {
    throw new RuntimeException("global boom");
});

// A delay could end before the queued coroutine has run: a timer due when main parks wakes it in place
while (!$failing->isCompleted()) {
    suspend();
}

echo "pool closed: ", var_export($pool->isClosed(), true), "\n";
try {
    $pool->spawn(fn() => print("pool spawn ran\n"));
} catch (Throwable $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

$global = spawn(fn() => print("global spawn ran\n"));
await($global);

try {
    await($failing);
} catch (Throwable $e) {
    echo "failing: ", $e->getMessage(), "\n";
}

?>
--EXPECT--
pool closed: true
Async\AsyncException: Cannot spawn a coroutine in a closed scope
global spawn ran
failing: global boom
