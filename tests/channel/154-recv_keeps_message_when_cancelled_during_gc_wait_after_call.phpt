--TEST--
Channel: recv() with a GC run due at the VM's release of the channel after the call keeps the message when the receiver is cancelled during the wait
--FILE--
<?php

use Async\Channel;
use Async\Future;
use Async\FutureState;

// As 151, with the channel out of the root buffer: the VM's release of `$this` after recv() returns then adds the
// root that starts the run, and the cancel the wait lets in is thrown with the message taken.
$channel = new Channel(1);
$channel->send("message");
gc_collect_cycles();
$token = Async\spawn(fn() => (new Future(new FutureState()))->await());
$held = [];
$runs = gc_status()['runs'];

$receiver = Async\spawn(function () use ($channel, $token, &$held) {
    $status = gc_status();

    for ($i = $status['threshold'] - $status['roots'] - 1; $i > 0; $i--) {
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
