--TEST--
Channel: a receiver outside the owner scope stays parked after the scope's coroutines end, and the script ends in the global deadlock
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use Async\Scope;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$scope = new Scope();
$holder = $scope->spawn(function () {
    delay(50);
});
$channel = await($scope->spawn(fn() => new Channel(0)));

spawn(function () use ($channel) {
    try {
        $channel->recv();
        echo "received\n";
    } catch (ChannelException $exception) {
        echo "receiver: ", $exception->reason->name, "\n";
    }
});

await($holder);
echo "the scope's coroutines ended: closed=", var_export($channel->isClosed(), true), "\n";
?>
--EXPECTF--
the scope's coroutines ended: closed=false

=== DEADLOCK REPORT START ===
Coroutines waiting: 1

Coroutine %d spawned at %s:%d, suspended at %s:%d
  waiting for:
    - Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)

=== DEADLOCK REPORT END   ===


Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 1 coroutines in waiting in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line %d
