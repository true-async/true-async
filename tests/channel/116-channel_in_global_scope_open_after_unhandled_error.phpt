--TEST--
Channel: a channel made in the global scope after an unhandled error stays open past the ends of the scope's coroutines
--FILE--
<?php

use Async\Channel;
use function Async\await;
use function Async\spawn;

$failed = spawn(function () {
    throw new Exception("boom");
});

while (!$failed->isCompleted()) {
    Async\suspend();
}

echo "main continues\n";

$channel = new Channel(1);
$sender = spawn(function () use ($channel) {
    $channel->send(1);
    echo "sent\n";
});
$idle = spawn(function () {
});
await($sender);
await($idle);
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
