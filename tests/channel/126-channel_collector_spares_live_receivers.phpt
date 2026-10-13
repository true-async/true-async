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

// The sleepers are live code until the three timers have fired, then the sender and the scope's
// holder act; main sleeps until all five receivers have reported.
$sleepers_after_report = [];
$reports = 0;

function sleep_until_cancelled(): void
{
    try {
        delay(100000);
    } catch (Async\AsyncCancellation) {
    }
}

function spawn_reporting(string $name, callable $body): void
{
    spawn(function () use ($name, $body) {
        global $sleepers_after_report, $reports;

        try {
            $body();
            echo $name, ": woke\n";
        } catch (Async\AsyncCancellation $cancellation) {
            echo $name, ": ", $cancellation->getMessage(), "\n";
        } catch (ChannelException $exception) {
            echo $name, ": ", $exception->reason->name, "\n";
        }

        $sleepers = $sleepers_after_report[++$reports] ?? [];

        foreach ($sleepers as $sleeper) {
            $sleeper->cancel();
        }
    });
}

$held = new Channel(0);
spawn_reporting("held by a sleeping coroutine", function () use ($held) {
    $held->recv();
});
$sleepers_after_report[3][] = spawn(function () use ($held) {
    sleep_until_cancelled();
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
$sleepers_after_report[3][] = spawn(function () use ($scope) {
    sleep_until_cancelled();
    $scope->cancel();
});

spawn_reporting("a recvAsync() Future on a timed channel", function () {
    $channel = new Channel(0, 150);
    $future = $channel->recvAsync();
    // A receiver's park arms the timer; its cancel leaves it armed for the Future.
    $parking = false;
    $receiver = spawn(function () use ($channel, &$parking) {
        $parking = true;

        // A cancel guarantees no state: the timer's close can reach the receiver before its cancellation does.
        try {
            $channel->recv();
        } catch (ChannelException) {
        }
    });

    // A coroutine runs on to its park, so the flag set means the receiver has parked.
    while (!$parking) {
        Async\suspend();
    }

    $receiver->cancel();
    Async\suspend();
    $future->await();
});
$sleepers_after_report[5][] = Async\current_coroutine();
sleep_until_cancelled();
echo "end\n";
?>
--EXPECT--
an armed soft timer: NO_PRODUCERS
an armed hard timer: NO_PRODUCERS
a recvAsync() Future on a timed channel: NO_PRODUCERS
held by a sleeping coroutine: woke
a bound scope a sleeping coroutine holds: SCOPE_DISPOSED
end
