--TEST--
Channel: the value a close from a timer, the owner scope or the global deadlock rolls back is released with the channel
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use Async\Scope;
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

$scope = new Scope();
$channel = await($scope->spawn(fn() => new Channel(0)));
$sender = spawn_sender($channel, "scope's");
Async\suspend();
$scope->dispose();
await($sender);
echo "freeing the channel\n";
unset($channel);

$channel = new Channel(0, 0, 10000);
$sender = spawn_sender($channel, "deadlock's");
unset($channel);
await($sender);
echo "end\n";
?>
--EXPECT--
sender of timer's: NO_CONSUMERS
freeing the channel
destroyed: timer's
sender of scope's: SCOPE_DISPOSED
freeing the channel
destroyed: scope's
sender of deadlock's: DEADLOCK
destroyed: deadlock's
end
