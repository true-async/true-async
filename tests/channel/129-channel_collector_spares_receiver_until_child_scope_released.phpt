--TEST--
Channel: the collector leaves alone a receiver on a channel of a cancelled scope whose free waits for a child scope's object a sleeping coroutine holds
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php

use Async\AsyncCancellation;
use Async\Channel;
use Async\ChannelException;
use Async\Scope;
use function Async\delay;
use function Async\spawn;

$child = $channel = null;
$scope = new Scope();
$scope->spawn(function () use (&$child, &$channel) {
    try {
        delay(1000);
    } catch (AsyncCancellation) {
        // Made after the cancel: the child scope is not cancelled, the channel binds to the cancelled scope.
        $child = Scope::inherit();
        $channel = new Channel(0);
    }
});
Async\suspend();
$scope->cancel();

while ($channel === null) {
    Async\suspend();
}

spawn(function () use (&$child) {
    delay(100);
    echo "releasing the child scope\n";
    $child = null;
});
spawn(function () use ($channel, $scope) {
    try {
        $channel->recv();
        echo "receiver: woke\n";
    } catch (ChannelException $exception) {
        echo "receiver: ", $exception->reason->name, "\n";
    } catch (AsyncCancellation $cancellation) {
        echo "receiver: ", $cancellation->getMessage(), "\n";
    }
});
unset($channel, $scope);
delay(200);
echo "end\n";
?>
--EXPECT--
releasing the child scope
receiver: SCOPE_DISPOSED
end
