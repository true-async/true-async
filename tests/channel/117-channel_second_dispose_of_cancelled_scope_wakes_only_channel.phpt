--TEST--
Channel: a dispose() of a cancelled owner scope closes a channel made after the cancel and wakes no awaitAfterCancellation() waiter
--FILE--
<?php

use Async\AsyncCancellation;
use Async\Channel;
use Async\ChannelException;
use Async\Scope;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$channel = null;
$scope = new Scope();
$scope->spawn(function () use (&$channel) {
    try {
        delay(1000);
    } catch (AsyncCancellation) {
        $channel = new Channel(0);
        delay(50);
        echo "member: ended\n";
    }
});
Async\suspend();
$scope->cancel();

while ($channel === null) {
    Async\suspend();
}

$waiter = spawn(function () use ($scope) {
    $scope->awaitAfterCancellation();
    echo "waiter: woke\n";
});
$receiver = spawn(function () use ($channel) {
    try {
        $channel->recv();
    } catch (ChannelException $exception) {
        echo "receiver: ", $exception->reason->name, "\n";
    }
});
Async\suspend();
echo "after the first cancel: closed=", var_export($channel->isClosed(), true), "\n";

$scope->dispose();
echo "after dispose(): closed=", var_export($channel->isClosed(), true), "\n";
Async\suspend();
echo "waiter still waits: ", var_export(!$waiter->isCompleted(), true), "\n";
await($waiter);
?>
--EXPECT--
after the first cancel: closed=false
after dispose(): closed=true
receiver: SCOPE_DISPOSED
waiter still waits: true
member: ended
waiter: woke
