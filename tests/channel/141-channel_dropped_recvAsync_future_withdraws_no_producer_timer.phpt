--TEST--
Channel: the last pending recvAsync() Future dropped withdraws the noProducerTimeout timer armed for it, so the idle channel stays open
--FILE--
<?php

use Async\Channel;
use function Async\await;
use function Async\delay;

$channel = new Channel(0, 200);
$first = $channel->recvAsync();
$second = $channel->recvAsync()->ignore();
$channel->sendAsync('a');
unset($second);
echo "first: ", await($first), "\n";
delay(300);
echo "closed: ", var_export($channel->isClosed(), true), "\n";
?>
--EXPECT--
first: a
closed: false
