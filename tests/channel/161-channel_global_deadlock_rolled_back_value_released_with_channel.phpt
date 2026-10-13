--TEST--
Channel: the value a close from the global deadlock rolls back is released with the channel
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

$channel = new Channel(0, 0, 10000);
$sender = spawn_sender($channel, "deadlock's");
unset($channel);
await($sender);
echo "end\n";
?>
--EXPECT--
sender of deadlock's: DEADLOCK
destroyed: deadlock's
end
