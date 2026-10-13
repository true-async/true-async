--TEST--
Channel: bailout (OOM) inside a coroutine holding channels — engine cleans up without crash
--INI--
memory_limit=8M
--FILE--
<?php

use Async\Channel;
use Async\Scope;
use function Async\spawn;
use function Async\suspend;
use function Async\await;

$scope = new Scope();
$ch = new Channel(2, 0, 0);

// Receiver in scope, blocks on the channel.
$scope->spawn(function () use ($ch) {
    try {
        $ch->recv();
    } catch (\Throwable $e) {
        // ignore
    }
});

// Producer that triggers an OOM bailout while the channel is alive
// and has a blocked receiver. Engine must unwind without UAF.
spawn(function () use ($ch) {
    suspend();
    $big = '';
    try {
        while (true) {
            $big .= str_repeat('A', 1024 * 1024);
        }
    } catch (\Throwable $e) {
        // OOM in PHP usually surfaces as fatal, but we try anyway
    }
});

// Main waits parked, as the receiver does, until the bailout ends the request.
await(new Async\Future(new Async\FutureState()));

echo "reached_end\n";
?>
--EXPECTF--
Fatal error: Allowed memory size of 8388608 bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
