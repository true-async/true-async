--TEST--
Channel: a value dropped by close() is released once the channel is consistent; its destructor may use the channel
--FILE--
<?php

use Async\Channel;

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

// An uncommitted rendezvous value withdrawn by close().
$closed = new Channel(0);
$closed->sendAsync(new Value($closed, "uncommitted"));
$closed->close();
echo "after close\n";
?>
--EXPECT--
uncommitted released: closed true, sendAsync false, count 0
after close
