--TEST--
Channel: the collector leaves alone receivers on a channel a sleeping coroutine holds, with an armed timer, or bound to a scope a sleeping coroutine holds
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use Async\Scope;
use function Async\delay;
use function Async\spawn;

function spawn_reporting(string $name, callable $body): void
{
    spawn(function () use ($name, $body) {
        try {
            $body();
            echo $name, ": woke\n";
        } catch (Async\AsyncCancellation $cancellation) {
            echo $name, ": ", $cancellation->getMessage(), "\n";
        } catch (ChannelException $exception) {
            echo $name, ": ", $exception->reason->name, "\n";
        }
    });
}

$held = new Channel(0);
spawn_reporting("held by a sleeping coroutine", function () use ($held) {
    $held->recv();
});
spawn(function () use ($held) {
    delay(200);
    $held->send(1);
});

spawn_reporting("an armed soft timer", function () {
    $channel = new Channel(0, 150);
    $channel->recv();
});
spawn_reporting("an armed hard timer", function () {
    $channel = new Channel(0, 150, 0, true);
    $channel->recv();
});

$scope = new Scope();
$scope->spawn(function () {
    $channel = new Channel(0);
    spawn_reporting("a bound scope a sleeping coroutine holds", function () use ($channel) {
        $channel->recv();
    });
});
spawn(function () use ($scope) {
    delay(200);
    $scope->cancel();
});

spawn_reporting("a recvAsync() Future on a timed channel", function () {
    $channel = new Channel(0, 150);
    $future = $channel->recvAsync();
    // A receiver's park arms the timer; its cancel leaves it armed for the Future.
    $receiver = spawn(function () use ($channel) {
        $channel->recv();
    });
    Async\suspend();
    $receiver->cancel();
    Async\suspend();
    $future->await();
});
delay(400);
echo "end\n";
?>
--EXPECT--
an armed soft timer: NO_PRODUCERS
an armed hard timer: NO_PRODUCERS
a recvAsync() Future on a timed channel: NO_PRODUCERS
held by a sleeping coroutine: woke
a bound scope a sleeping coroutine holds: SCOPE_DISPOSED
end
