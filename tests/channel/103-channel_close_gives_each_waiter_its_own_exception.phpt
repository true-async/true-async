--TEST--
Channel: close() wakes each waiter with an exception of its own, so one waiter's cancellation reaches no other
--FILE--
<?php

use Async\Channel;
use function Async\spawn;
use function Async\suspend;

function describe(Throwable $exception): string
{
    $previous = $exception->getPrevious();

    return get_class($exception) . ", previous " . ($previous === null ? "none" : get_class($previous));
}

$channel = new Channel();
$cancelled = spawn(function () use ($channel) {
    try {
        $channel->recv();
    } catch (Throwable $exception) {
        echo "cancelled receiver: ", describe($exception), "\n";
    }
});
$receiver = spawn(function () use ($channel) {
    try {
        $channel->recv();
    } catch (Throwable $exception) {
        echo "receiver: ", describe($exception), "\n";
    }
});
$future = $channel->recvAsync();
$future->ignore();

suspend();
$cancelled->cancel();
$channel->close();
suspend();

$future->catch(fn(Throwable $exception) => print("future: " . describe($exception) . "\n"))->ignore();
suspend();
?>
--EXPECT--
cancelled receiver: Async\ChannelException, previous Async\AsyncCancellation
receiver: Async\ChannelException, previous none
future: Async\ChannelException, previous none
