--TEST--
Channel: a receiver whose noProducerTimeout timer cannot be armed for want of an IO queue is not parked, its recv() throws the failure, and the channel stays open
--FILE--
<?php
use TrueAsync\Test;

$channel = new Async\Channel(0, noProducerTimeout: 10);

$receiver = Async\spawn(function () use ($channel) {
    Test\fail_io('queue_create');

    try {
        $channel->recv();
        echo "not reached\n";
    } catch (Error $error) {
        echo get_class($error), ": ", $error->getMessage(), "\n";
    }

    var_dump($channel->isClosed());
});

Async\await($receiver);
?>
--EXPECT--
Error: Cannot create the IO queue
bool(false)
