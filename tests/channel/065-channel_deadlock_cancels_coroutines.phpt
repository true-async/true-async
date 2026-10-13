--TEST--
Channel: with no soft-timer, deadlock detector cancels coroutines, channel still freed cleanly
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use function Async\spawn;
use function Async\await;

// Disabled timeouts → no soft registration → no global resolver path.
// Lone receiver blocks → scheduler hits resolve_deadlocks → cancels.
$ch = new Channel(0, 0, 0);

$task = spawn(function () use ($ch) {
    try {
        $ch->recv();
        return "FAIL_RETURNED";
    } catch (\Throwable $e) {
        echo "recv_threw=", get_class($e), "\n";
        return get_class($e);
    }
});

try {
    $result = await($task);
    echo "result=", $result, "\n";
} catch (\Throwable $e) {
    echo "await_threw=", get_class($e), "\n";
}
echo "ch_closed=", $ch->isClosed() ? "true" : "false", "\n";
?>
--EXPECTF--
=== DEADLOCK REPORT START ===
Coroutines waiting: 2

Coroutine %d spawned at %s, suspended at %s:%d
  waiting for:
    - await: coroutine #%d

Coroutine %d spawned at %s:%d, suspended at %s:%d
  waiting for:
    - Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)

=== DEADLOCK REPORT END   ===

await_threw=Async\AsyncCancellation
ch_closed=false
recv_threw=Async\AsyncCancellation

Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 2 coroutines in waiting in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
