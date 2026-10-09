--TEST--
TaskGroup: dispose() closes a channel made by one of its tasks, waking a receiver in another task
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$made = new FutureState();
$waiting = new FutureState();
$group = new TaskGroup();

$group->spawn(function () use ($made) {
    $made->complete(new Channel());
    (new Future(new FutureState()))->await();
});

$channel = (new Future($made))->await();

$group->spawn(function () use ($channel, $waiting) {
    $waiting->complete(null);

    try {
        $channel->recv();
    } catch (ChannelException $error) {
        echo "recv: ", $error->reason->name, "\n";
    }
});

(new Future($waiting))->await();
$group->dispose();
$group->all(ignoreErrors: true)->await();

var_dump($channel->isClosed());
?>
--EXPECT--
recv: SCOPE_DISPOSED
bool(true)
