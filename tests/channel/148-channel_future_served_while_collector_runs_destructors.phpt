--TEST--
Channel: a coroutine woken by a destructor of a collection serves a recvAsync() Future of a dropped cycle; the child gets the value and nothing is freed under the drain
--FILE--
<?php

use Async\Channel;
use Async\Future;
use Async\FutureState;
use function Async\spawn;

final class CompletesOnDestruct
{
    public ?CompletesOnDestruct $self = null;

    public function __construct(private FutureState $state)
    {
    }

    public function __destruct()
    {
        echo "destructor completes the state\n";
        $this->state->complete(null);
    }
}

$channel = new Channel(1);
$state = new FutureState();

spawn(function () use ($channel, $state) {
    (new Future($state))->await();
    echo "sender sends: ", var_export($channel->sendAsync('value'), true), "\n";
});

Async\suspend();

// The source and its child hold each other; only the collector sees the pair.
$future = $channel->recvAsync();
$future->map(function ($value) {
    echo "mapped: ", $value, "\n";
})->ignore();
$future->ignore();
unset($future);

$hook = new CompletesOnDestruct($state);
$hook->self = $hook;
unset($hook);

gc_collect_cycles();
echo "after the collection\n";
Async\suspend();
echo "count: ", count($channel), "\n";
?>
--EXPECT--
destructor completes the state
sender sends: true
mapped: value
after the collection
count: 0
