--TEST--
Coroutine: isSuspended() returns false once the coroutine has finished
--FILE--
<?php

use function Async\spawn;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\await_all;
use function Async\delay;
use function Async\suspend;

// 1. Normal return — predicate set must collapse to {started, completed}
$ok = spawn(fn() => 42);
await_all([$ok]);
echo "ok: started=",     $ok->isStarted()    ? "y" : "n", "\n";
echo "ok: completed=",   $ok->isCompleted()  ? "y" : "n", "\n";
echo "ok: suspended=",   $ok->isSuspended()  ? "y" : "n", "\n";
echo "ok: running=",     $ok->isRunning()    ? "y" : "n", "\n";

// 2. Throw — same rule
$bad = spawn(function() { throw new RuntimeException("x"); });
try { await_all([$bad]); } catch (\Throwable $e) {}
echo "bad: completed=",  $bad->isCompleted() ? "y" : "n", "\n";
echo "bad: suspended=",  $bad->isSuspended() ? "y" : "n", "\n";

// 3. Cancellation — same rule
$slow_parked = false;
$slow = spawn(function() use (&$slow_parked) { $slow_parked = true; delay(100000); });
$canceller = spawn(function() use ($slow, &$slow_parked) {
    while (!$slow_parked) { suspend(); }
    $slow->cancel();
});
await_all([$canceller]);
try { await($slow); } catch (\Async\AsyncCancellation) {}
echo "slow: completed=", $slow->isCompleted() ? "y" : "n", "\n";
echo "slow: cancelled=", $slow->isCancelled() ? "y" : "n", "\n";
echo "slow: suspended=", $slow->isSuspended() ? "y" : "n", "\n";

// 4. While actually suspended — must be true
// It sleeps until the inspector is done: a timer could end its wait first.
$wake = new FutureState();
$sleeping_parked = false;
$sleeping = spawn(function() use ($wake, &$sleeping_parked) {
    $sleeping_parked = true;
    await(new Future($wake));
});
$inspector = spawn(function() use ($sleeping, $wake, &$sleeping_parked) {
    while (!$sleeping_parked) { suspend(); }
    echo "during: suspended=", $sleeping->isSuspended() ? "y" : "n", "\n";
    echo "during: completed=", $sleeping->isCompleted() ? "y" : "n", "\n";
    $wake->complete(null);
});
await_all([$sleeping, $inspector]);

?>
--EXPECT--
ok: started=y
ok: completed=y
ok: suspended=n
ok: running=n
bad: completed=y
bad: suspended=n
slow: completed=y
slow: cancelled=y
slow: suspended=n
during: suspended=y
during: completed=n
