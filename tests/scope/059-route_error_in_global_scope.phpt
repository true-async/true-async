--TEST--
Scope: an unawaited error of a coroutine of the global scope cancels the queued coroutines and makes the started ones zombies
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\suspend;

// TrueAsync's route, probed on its build (dev/plans/S9-scope.md 4, p5.php): the global scope disposes
// safely, so main runs on as a zombie and only $queued, which has not started, is cancelled.
$failing = spawn(function () {
    throw new RuntimeException("boom");
});
$queued = spawn(function () {
    echo "queued ran\n";
});

suspend();

echo "queued cancelled: ", var_export($queued->isCancelled(), true), "\n";

try {
    await($queued);
} catch (Throwable $e) {
    echo "queued: ", get_class($e), ": ", $e->getMessage(), "\n";
}

try {
    await($failing);
} catch (RuntimeException $e) {
    echo "failing: ", $e->getMessage(), "\n";
}

$late = spawn(function () {
    echo "late ran\n";
});
await($late);

?>
--EXPECT--
queued cancelled: true
queued: Async\AsyncCancellation: Coroutine cancelled
failing: boom
late ran
