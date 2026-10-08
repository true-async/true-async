--TEST--
Channel: a channel made at the top level is open in a shutdown function and in a destructor at shutdown
--FILE--
<?php

use Async\Channel;

$channel = new Channel(1);
$channel->sendAsync('kept');

register_shutdown_function(function () use ($channel) {
    echo "shutdown function: closed=", var_export($channel->isClosed(), true), "\n";
});

final class Holder
{
    public function __construct(public Channel $channel) {}

    public function __destruct()
    {
        echo "destructor: closed=", var_export($this->channel->isClosed(), true), ", ", $this->channel->recv(), "\n";
    }
}

$holder = new Holder($channel);
echo "end of script\n";
?>
--EXPECT--
end of script
shutdown function: closed=false
destructor: closed=false, kept
