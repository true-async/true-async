--TEST--
Channel: an await_* item completes with the close, not with a value; await() refuses a channel
--FILE--
<?php

use Async\Channel;
use function Async\await;
use function Async\await_any_or_fail;
use function Async\spawn;
use function Async\suspend;

$channel = new Channel(1);

try {
    await($channel);
} catch (TypeError $error) {
    echo "await: ", $error->getMessage(), "\n";
}

$item = spawn(function () use ($channel) {
    try {
        await_any_or_fail([$channel]);
    } catch (Async\ChannelException $exception) {
        echo "item: ", $exception->getMessage(), "\n";
    }
});

suspend();
var_dump($item->getAwaitingInfo());

$channel->sendAsync('value');
echo "a value completes nothing: ", var_export($item->isCompleted(), true), "\n";

$channel->close();
await($item);

try {
    await_any_or_fail([$channel]);
} catch (Async\ChannelException $exception) {
    echo "closed item: ", $exception->getMessage(), "\n";
}
?>
--EXPECT--
await: Async\await(): Argument #1 ($awaitable) must be of type Async\Completable, Async\Channel given
array(1) {
  [0]=>
  string(14) "await: channel"
}
a value completes nothing: false
item: Channel is closed
closed item: Channel is closed
