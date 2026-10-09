<?php
// B15: a rendezvous ping-pong between two coroutines (dev/plans/S9-channel.md, section 9).
// Usage: b15.php <messages> [known]. The first coroutine sends on `ping` and waits on `pong`, the
// second answers each message; both channels have capacity 0. One operation is one message.
// "known" sends a `new stdClass` instead of the counter: the known-answer variant.
use Async\Channel;
use function Async\await;
use function Async\spawn;

gc_disable();
$messages = (int) ($argv[1] ?? 100000);
$known = ($argv[2] ?? '') === 'known';
$rounds = intdiv($messages, 2);
$ping = new Channel(0);
$pong = new Channel(0);

$first = spawn(static function () use ($ping, $pong, $rounds, $known) {
    for ($i = 0; $i < $rounds; $i++) {
        $ping->send($known ? new stdClass() : $i);
        $pong->recv();
    }
});

$second = spawn(static function () use ($ping, $pong, $rounds, $known) {
    for ($i = 0; $i < $rounds; $i++) {
        $ping->recv();
        $pong->send($known ? new stdClass() : $i);
    }
});

await($first);
await($second);
