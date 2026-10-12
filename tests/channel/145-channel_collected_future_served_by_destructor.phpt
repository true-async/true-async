--TEST--
Channel: a recvAsync() Future that only its map() child holds stays queued while the channel lives, and the destructor its mapper releases sends after the value
--FILE--
<?php

use Async\Channel;
use Async\Future;
use Async\FutureState;

final class SendsOnDestruct
{
    public function __construct(private Channel $channel, private FutureState $done)
    {
    }

    public function __destruct()
    {
        echo "destructor sends: ", var_export($this->channel->sendAsync('second'), true), "\n";
        $this->done->complete(null);
    }
}

$channel = new Channel(1);
$future = $channel->recvAsync();
$done = new FutureState();
$hook = new SendsOnDestruct($channel, $done);
// The source holds the child in its chain, and the child holds the source and, through its mapper, the hook.
$future->map(function ($value) use ($hook) {
    echo "mapped: ", $value, "\n";
})->ignore();
unset($hook);
$future->ignore();
unset($future);

var_dump(gc_collect_cycles());
$channel->send('first');
(new Future($done))->await();
echo "count: ", count($channel), "\n";
?>
--EXPECT--
int(0)
mapped: first
destructor sends: true
count: 1
