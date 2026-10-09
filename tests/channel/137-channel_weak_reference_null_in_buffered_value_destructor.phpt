--TEST--
Channel: a buffered value's destructor run by the channel's free finds the channel's WeakReference empty
--FILE--
<?php

use Async\Channel;

final class Value
{
    public static WeakReference $channel;

    public function __destruct()
    {
        echo "channel reachable: ", var_export(self::$channel->get() !== null, true), "\n";
    }
}

$channel = new Channel(2);
Value::$channel = WeakReference::create($channel);
$channel->sendAsync(new Value());
unset($channel);
echo "end\n";
?>
--EXPECT--
channel reachable: false
end
