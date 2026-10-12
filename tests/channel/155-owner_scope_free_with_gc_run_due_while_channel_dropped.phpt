--TEST--
Channel: the close by its owner scope's free, with a GC run due inside it, goes on with no channel that another coroutine freed
--FILE--
<?php

use Async\Channel;
use Async\Scope;

// A pending recvAsync() Future does not hold the channel. One root short of the threshold puts the release of its
// ChannelException inside the close, where a wait for the collection would let the queued coroutine drop the last
// reference and free the channel before the close goes on.
$scope = new Scope();
$channel = Async\await($scope->spawn(fn() => new Channel()));
$future = $channel->recvAsync();

Async\spawn(function () use (&$channel) {
    $channel = null;
    echo "channel dropped\n";
});

$held = [];
$status = gc_status();

for ($i = $status['threshold'] - $status['roots'] - 1; $i > 0; $i--) {
    $object = new stdClass();
    $held[] = $object;
    unset($object);
}

$runs = gc_status()['runs'];
$scope = null;

Async\suspend();

try {
    $future->await();
} catch (Async\ChannelException $exception) {
    echo "recvAsync(): ", $exception->getMessage(), "\n";
}

echo "collection after: ", gc_status()['runs'] > $runs ? "yes" : "no", "\n";
?>
--EXPECT--
channel dropped
recvAsync(): Channel closed: owner scope disposed
collection after: yes
