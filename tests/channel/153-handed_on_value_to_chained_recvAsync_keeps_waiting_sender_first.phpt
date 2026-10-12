--TEST--
Channel: a buffered value handed on from a cancelled receiver to a pending recvAsync() that has a map() child, with a GC run queued when the channel lets the Future go, gives the freed slot to the sender already waiting, not to a later one
--FILE--
<?php

use Async\Channel;

// The receiver is promised the first value and cancelled before it takes it, so its frame hands the value on to
// the Future. One root short of the threshold puts the release of the channel's hold on the Future inside that
// hand-on. Before S9.38 the core waited for the collection there, which let the third sender take the slot the
// second one waits for; it now queues the collection, and only a coroutine 100 000 roots past it waits, which the
// receiver, resumed by its cancel, cannot be. The test keeps the order: the second sender's value comes first.
$channel = new Channel(1);
$receiver = Async\spawn(function () use ($channel) {
    try {
        $value = $channel->recv();
        echo "not reached\n";
        // Under another order (a random scheduling seed) it takes the value: given back past the cancel, main's
        // receives end.
        Async\protect(fn () => $channel->send($value));
    } catch (Async\AsyncCancellation $exception) {
        echo "receiver cancelled\n";
    }
});
Async\suspend();

$future = $channel->recvAsync();
$child = $future->map(fn ($value) => $value);
// Empties the root buffer, so the release of the hold adds the Future as a root. Before the senders: the
// collection waits in this coroutine.
gc_collect_cycles();

Async\spawn(function () use ($channel) {
    $channel->send(1);
});
Async\spawn(function () use ($channel) {
    $channel->send(2);
});
Async\suspend();

$receiver->cancel();
// Queued behind the receiver, so it runs first only inside the receiver's hand-on.
Async\spawn(function () use ($channel) {
    echo "third sender runs\n";
    $channel->send(3);
});

$held = [];
$status = gc_status();

for ($i = $status['threshold'] - $status['roots'] - 1; $i > 0; $i--) {
    $object = new stdClass();
    $held[] = $object;
    unset($object);
}

$runs = gc_status()['runs'];
$mapped = $child->await();
echo "map(): ", $mapped, "\n";
echo "recv(): ", $channel->recv(), "\n";
echo "recv(): ", $channel->recv(), "\n";
echo "collection after: ", gc_status()['runs'] > $runs ? "yes" : "no", "\n";
?>
--EXPECT--
receiver cancelled
third sender runs
map(): 1
recv(): 2
recv(): 3
collection after: yes
