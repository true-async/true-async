--TEST--
Channel: an explicit close() ends foreach quietly, but not a cancellation it finds queued
--FILE--
<?php

use Async\Channel;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

function consume(Channel $channel): Async\Coroutine
{
    return spawn(function () use ($channel) {
        try {
            foreach ($channel as $value) {
                echo "got $value\n";
            }

            echo "loop ended quietly\n";
        } catch (Throwable $exception) {
            echo get_class($exception), ", previous ", get_class($exception->getPrevious()), "\n";
            throw $exception;
        }
    });
}

$quiet = new Channel();
$consumer = consume($quiet);
suspend();
$quiet->close();
await($consumer);

$cancelled = new Channel();
$consumer = consume($cancelled);
suspend();
$consumer->cancel();
$cancelled->close();

try {
    await($consumer);
} catch (Throwable $exception) {
    echo "await: ", get_class($exception), "\n";
}

echo "cancelled: ", var_export($consumer->isCancelled(), true), "\n";
?>
--EXPECT--
loop ended quietly
Async\ChannelException, previous Async\AsyncCancellation
await: Async\ChannelException
cancelled: true
