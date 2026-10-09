--TEST--
Channel: a channel closed inside a function that took it is freed at its last unset(), with its unreceived value
--INI--
zend.exception_ignore_args=0
--FILE--
<?php

use Async\Channel;

final class Value
{
    public function __destruct()
    {
        echo "value destroyed\n";
    }
}

function close_channel(Channel $channel): void
{
    $channel->close();
}

$channel = new Channel(1);
$channel->sendAsync(new Value());
close_channel($channel);
unset($channel);
echo "after unset\n";
?>
--EXPECT--
value destroyed
after unset
