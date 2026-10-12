--TEST--
Channel: send() to a pending recvAsync() that has a map() child, with a GC run due when the channel lets the Future go, leaves a later sender's value uncommitted: that send() fails at close() and its value does not stay in the channel
--FILE--
<?php

use Async\Channel;

// The child makes the channel hold the Future until the Future completes. One root short of the threshold puts
// the release of that hold inside send(), after the take, where a wait for the collection would let the second
// sender put its value in the free slot before the first send() marks the slot committed.
$channel = new Channel();
$first = new stdClass();
$first->name = "first";
$second = new stdClass();
$second->name = "second";
$future = $channel->recvAsync();
$child = $future->map(fn ($value) => $value->name);
// Empties the root buffer, so the release of the hold adds the Future as a root. Before the spawn: the collection
// waits in this coroutine.
gc_collect_cycles();

// Runs inside the first send(): main does not suspend before it.
Async\spawn(function () use ($channel, $second) {
    echo "second sender runs\n";

    try {
        $channel->send($second);
        echo "second send(): delivered\n";
    } catch (Async\ChannelException $exception) {
        echo "second send(): ", $exception->getMessage(), "\n";
    }
});

$held = [];
$status = gc_status();

for ($i = $status['threshold'] - $status['roots'] - 1; $i > 0; $i--) {
    $object = new stdClass();
    $held[] = $object;
    unset($object);
}

$runs = gc_status()['runs'];
$channel->send($first);
echo "first send(): returned\n";
echo "map(): ", $child->await(), "\n";

Async\suspend();
$channel->close();
Async\suspend();

try {
    $channel->recv();
    echo "not reached\n";
} catch (Async\ChannelException $exception) {
    echo "recv() after close(): ", $exception->getMessage(), "\n";
}

echo "collection after: ", gc_status()['runs'] > $runs ? "yes" : "no", "\n";
?>
--EXPECT--
second sender runs
first send(): returned
map(): first
second send(): Channel is closed
recv() after close(): Channel is closed
collection after: yes
