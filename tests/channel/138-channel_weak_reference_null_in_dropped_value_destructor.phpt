--TEST--
Channel: a value a timer's close rolled back, released by the channel's free, finds the channel's WeakReference empty
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use function Async\await;
use function Async\spawn;

final class Value
{
    public static WeakReference $channel;

    public function __destruct()
    {
        echo "channel reachable: ", var_export(self::$channel->get() !== null, true), "\n";
    }
}

$channel = new Channel(0, 0, 10, true);
Value::$channel = WeakReference::create($channel);
$sender = spawn(function () use ($channel) {
    try {
        $channel->send(new Value());
    } catch (ChannelException $exception) {
        echo "sender: ", $exception->reason->name, "\n";
    }
});
await($sender);
unset($sender, $channel);
echo "end\n";
?>
--EXPECT--
sender: NO_CONSUMERS
channel reachable: false
end
