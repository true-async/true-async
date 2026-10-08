--TEST--
Channel: a channel made in the global scope after an unhandled error stays open past the ends of the scope's coroutines
--FILE--
<?php

use Async\Channel;
use function Async\delay;
use function Async\spawn;

$failed = spawn(function () {
    throw new Exception("boom");
});
delay(10);
echo "main continues\n";

$channel = new Channel(1);
spawn(function () use ($channel) {
    delay(20);
    $channel->send(1);
    echo "sent\n";
});
spawn(function () {
    delay(5);
});
delay(50);
echo "closed: ", var_export($channel->isClosed(), true), "\n";
echo "received: ", $channel->recv(), "\n";
?>
--EXPECTF--
main continues
sent
closed: false
received: 1

Fatal error: Uncaught Exception: boom in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d
