--TEST--
Channel: recv() with a coroutine as its cancellation token and a GC run due after the take returns the message, though the receiver is cancelled right after
--FILE--
<?php

use Async\Channel;
use Async\Future;
use Async\FutureState;

// One root short of the core's wait for a queued collection (100 000 roots past the threshold) puts the token's
// release after the take inside recv(), where a wait for the collection would let the queued coroutine cancel the
// receiver and recv() throw with the message taken.
$channel = new Channel(1);
$channel->send("message");
$token = Async\spawn(fn() => (new Future(new FutureState()))->await());
$held = [];
$runs = gc_status()['runs'];

$receiver = Async\spawn(function () use ($channel, $token, &$held) {
    $status = gc_status();

    for ($i = $status['threshold'] - $status['roots'] - 1 + 100000; $i > 0; $i--) {
        $object = new stdClass();
        $held[] = $object;
        unset($object);
    }

    echo "recv(): ", $channel->recv($token), "\n";
});

Async\spawn(function () use (&$receiver) {
    $receiver->cancel();
    echo "receiver cancelled\n";
});

// Whether the cancel still reaches the receiver after recv() rests on later waits; the message is what is checked.
try {
    Async\await($receiver);
} catch (Async\AsyncCancellation) {
}

echo "left in the channel: ", count($channel), "\n";
echo "collection after: ", gc_status()['runs'] > $runs ? "yes" : "no", "\n";
$token->cancel();
?>
--EXPECT--
recv(): message
receiver cancelled
left in the channel: 0
collection after: yes
