--TEST--
Channel: a later await_* of a closed channel does not see the error an earlier wait chained under it
--FILE--
<?php

use Async\Channel;
use Async\Future;
use Async\FutureState;
use function Async\await_any_or_fail;

$state = new FutureState();
$failed = new Future($state);
$state->error(new LogicException("failed"));

$channel = new Channel(1);
$channel->close();

try {
    await_any_or_fail([$failed, $channel]);
} catch (Async\ChannelException $exception) {
    echo "first: ", $exception->getPrevious()::class, "\n";
}

try {
    await_any_or_fail([$channel]);
} catch (Async\ChannelException $exception) {
    echo "second: ", var_export($exception->getPrevious(), true), "\n";
}
?>
--EXPECT--
first: LogicException
second: NULL
