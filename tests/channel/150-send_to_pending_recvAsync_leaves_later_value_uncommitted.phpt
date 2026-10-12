--TEST--
Channel: send() to a pending recvAsync() with a GC run due after the take leaves a later sender's value uncommitted: that send() fails at close() and its value does not stay in the channel
--FILE--
<?php

use Async\Channel;

// The Future takes the first value at once. One root short of the threshold puts a release after the take inside
// send(), where a wait for the collection would let the second sender put its value in the free slot before the
// first send() marks the slot committed: the value would then outlive the second send()'s failure.
$channel = new Channel();
$first = new stdClass();
$first->name = "first";
$second = new stdClass();
$second->name = "second";
$future = $channel->recvAsync();

Async\spawn(function () use ($channel, $second) {
    try {
        $channel->send($second);
        echo "not reached\n";
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
echo "recvAsync(): ", $future->await()->name, "\n";

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
recvAsync(): first
second send(): Channel is closed
recv() after close(): Channel is closed
collection after: yes
