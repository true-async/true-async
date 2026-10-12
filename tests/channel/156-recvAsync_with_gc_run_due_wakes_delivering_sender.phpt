--TEST--
Channel: recvAsync() of a rendezvous value with a GC run due after the take wakes that value's sender, and a sender run meanwhile waits for its own value to be taken
--FILE--
<?php

use Async\Channel;

// The first sender waits for its value to be taken. One root short of the core's wait for a queued collection (100
// 000 roots past the threshold) puts a release after the take inside recvAsync(), where a wait for the collection
// would let the second sender put its value in the free slot before the first sender is woken; the first would then
// wait on, and the second be woken for its value.
$channel = new Channel();
$first = new stdClass();
$first->name = "first";
$second = new stdClass();
$second->name = "second";

Async\spawn(function () use ($channel, $first) {
    $channel->send($first);
    echo "first send() returned\n";
});

Async\suspend();

Async\spawn(function () use ($channel, $second) {
    $channel->send($second);
    echo "second send() returned\n";
});

$held = [];
$status = gc_status();

for ($i = $status['threshold'] - $status['roots'] - 1 + 100000; $i > 0; $i--) {
    $object = new stdClass();
    $held[] = $object;
    unset($object);
}

$runs = gc_status()['runs'];
$future = $channel->recvAsync();
$taken = $future->await();
echo "recvAsync(): ", $taken->name, "\n";
$received = $channel->recv();
echo "recv(): ", $received->name, "\n";

Async\suspend();
echo "collection after: ", gc_status()['runs'] > $runs ? "yes" : "no", "\n";
?>
--EXPECT--
first send() returned
recvAsync(): first
recv(): second
second send() returned
collection after: yes
