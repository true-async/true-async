--TEST--
Channel: the value a close from a timer rolls back is released with the channel
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use function Async\await;
use function Async\spawn;

final class Value
{
    public function __construct(private string $name) {}

    public function __destruct()
    {
        echo "destroyed: ", $this->name, "\n";
    }
}

function spawn_sender(Channel $channel, string $name): Async\Coroutine
{
    return spawn(function () use ($channel, $name) {
        try {
            $channel->send(new Value($name));
        } catch (ChannelException $exception) {
            echo "sender of ", $name, ": ", $exception->reason->name, "\n";
        }
    });
}

$channel = new Channel(0, 0, 20, true);
await(spawn_sender($channel, "timer's"));
echo "freeing the channel\n";
unset($channel);
echo "end\n";
?>
--EXPECT--
sender of timer's: NO_CONSUMERS
freeing the channel
destroyed: timer's
end
