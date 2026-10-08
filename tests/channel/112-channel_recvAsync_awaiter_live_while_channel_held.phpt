--TEST--
Channel: a coroutine awaiting a recvAsync() Future is live while someone else holds the channel, and found never to wake when only it, or its partner in a cycle, does
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php

use Async\Channel;
use function Async\delay;
use function Async\spawn;

function await_reported(string $name, Async\Future $future): void
{
    try {
        $future->await();
    } catch (Async\AsyncCancellation $cancellation) {
        echo $name, ": ", $cancellation->getMessage(), "\n";
    }
}

function start(): void
{
    $channel = new Channel(0);
    $handed = $channel->recvAsync();

    spawn(function () use ($channel) {
        $value = $channel->recvAsync()->await();
        echo "holding the channel: ", $value, "\n";
    });

    spawn(function () use ($handed) {
        $value = $handed->await();
        echo "holding only the Future: ", $value, "\n";
    });

    spawn(function () use ($channel) {
        delay(50);
        $channel->send('first');
        $channel->send('second');
    });

    // A weakly referenced channel's node is live from the start, so the walk never reads it.
    $weak_channel = new Channel(0);
    $GLOBALS['weak'] = WeakReference::create($weak_channel);

    spawn(function () use ($weak_channel) {
        $value = $weak_channel->recvAsync()->await();
        echo "weakly referenced: ", $value, "\n";
    });

    spawn(function () use ($weak_channel) {
        delay(50);
        $weak_channel->send('third');
    });

    spawn(function () {
        $alone = new Channel(1);
        await_reported("alone", $alone->recvAsync());
    });

    spawn(function () {
        $alone = new Channel(1);
        $alone->send('value');

        foreach ($alone as $value) {
            await_reported("alone in foreach", $alone->recvAsync());
            break;
        }
    });

    $first = new Channel(0);
    $second = new Channel(0);

    spawn(function () use ($first, $second) {
        await_reported("cycle, first", $first->recvAsync());
    });

    spawn(function () use ($second, $first) {
        await_reported("cycle, second", $second->recvAsync());
    });
}

start();
?>
--EXPECTF--
%A
alone: Deadlock detected
alone in foreach: Deadlock detected
cycle, first: Deadlock detected
cycle, second: Deadlock detected
holding only the Future: first
holding the channel: second
weakly referenced: third
