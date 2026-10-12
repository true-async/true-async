--TEST--
Channel: the collector finds receivers on a channel only they hold, a foreach parked in its first receive and the iterator_*() functions among them
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php

use Async\Channel;
use function Async\delay;
use function Async\spawn;

// The producer and main sleep as live code until the collector's runs have reported, in turn, the seven
// receivers no one else holds and the one the ended producer leaves alone.
$sleeper_after_report = [];
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
        global $sleeper_after_report, $reports;

        try {
            $body();
            echo $name, ": woke\n";
        } catch (Async\AsyncCancellation $cancellation) {
            echo $name, ": ", $cancellation->getMessage(), "\n";

            $sleeper = $sleeper_after_report[++$reports] ?? null;

            if ($sleeper !== null) {
                // From a coroutine of its own: a cancel made here would keep this frame's values in its trace.
                spawn(fn() => $sleeper->cancel());
            }
        }
    });
}

function forgetful_producer(): Channel
{
    global $sleeper_after_report;

    $channel = new Channel(0);
    $sleeper_after_report[7] = spawn(function () use ($channel) {
        sleep_until_cancelled();
    });

    return $channel;
}

final class Aggregate implements IteratorAggregate
{
    public function __construct(public Channel $channel) {}

    public function getIterator(): Traversable
    {
        return $this->channel;
    }
}

spawn_reporting("recv()", function () {
    $channel = new Channel(0);
    $channel->recv();
});
spawn_reporting("foreach over a variable", function () {
    $channel = new Channel(0);
    foreach ($channel as $value) {
    }
});
spawn_reporting("foreach over a new channel", function () {
    foreach (new Channel(0) as $value) {
    }
});
spawn_reporting("foreach after a producer that never closes", function () {
    foreach (forgetful_producer() as $value) {
    }
});
spawn_reporting("foreach over an aggregate", function () {
    foreach (new Aggregate(new Channel(0)) as $value) {
    }
});
spawn_reporting("iterator_to_array()", function () {
    iterator_to_array(new Channel(0));
});
spawn_reporting("iterator_count()", function () {
    iterator_count(new Channel(0));
});
spawn_reporting("iterator_apply()", function () {
    $channel = new Channel(0);
    iterator_apply($channel, fn() => true);
});
$sleeper_after_report[8] = Async\current_coroutine();
sleep_until_cancelled();
echo "end\n";
?>
--EXPECTF--
Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)) in %s on line %d

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)) in %s on line %d

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)) in %s on line %d

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)) in %s on line %d

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)) in %s on line %d

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)) in %s on line %d

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)) in %s on line %d
recv(): Deadlock detected
foreach over a variable: Deadlock detected
foreach over a new channel: Deadlock detected
foreach over an aggregate: Deadlock detected
iterator_to_array(): Deadlock detected
iterator_count(): Deadlock detected
iterator_apply(): Deadlock detected

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)) in %s on line %d
foreach after a producer that never closes: Deadlock detected
end
