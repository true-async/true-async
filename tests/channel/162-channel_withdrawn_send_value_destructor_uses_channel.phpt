--TEST--
Channel: the value of a withdrawn send() is released once the channel is consistent; its destructor may use the channel
--FILE--
<?php

use Async\AsyncCancellation;
use Async\Channel;
use function Async\spawn;
use function Async\suspend;
use function Async\await;

final class Value
{
    public function __construct(private Channel $channel, private string $name) {}

    public function __destruct()
    {
        echo $this->name, " released: closed ", var_export($this->channel->isClosed(), true),
            ", sendAsync ", var_export($this->channel->sendAsync("again"), true),
            ", count ", count($this->channel), "\n";
    }
}

// The value of a rendezvous send cancelled before a receiver took it.
$open = new Channel(0);
$sender = spawn(function () use ($open) {
    try {
        $open->send(new Value($open, "withdrawn"));
    } catch (AsyncCancellation $exception) {
        echo "sender: ", get_class($exception), "\n";
    }
});
suspend();
$sender->cancel();
await($sender);
echo "recv: ", $open->recv(), "\n";
?>
--EXPECT--
withdrawn released: closed false, sendAsync true, count 1
sender: Async\AsyncCancellation
recv: again
