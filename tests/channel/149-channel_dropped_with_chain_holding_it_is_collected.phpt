--TEST--
Channel: a dropped channel whose pending recvAsync() chain holds it is collected, and its destructor fails the chain
--FILE--
<?php

use Async\Channel;
use Async\Future;
use Async\FutureState;

function arm(FutureState $done): WeakReference
{
    $channel = new Channel(1);
    $channel->recvAsync()->catch(function (Throwable $error) use ($channel, $done) {
        echo get_class($error), ": ", $error->getMessage(), "\n";
        $done->complete(null);
    })->ignore();

    return WeakReference::create($channel);
}

$done = new FutureState();
$reference = arm($done);
gc_collect_cycles();
(new Future($done))->await();
gc_collect_cycles();
var_dump($reference->get());
?>
--EXPECT--
Async\ChannelException: Channel disposed
NULL
