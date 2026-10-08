--TEST--
Channel: 10 000 recvAsync() Futures abandoned by a Timeout leave the queue and memory as they were
--FILE--
<?php

use Async\Channel;
use Async\OperationCanceledException;
use function Async\await_any_or_fail;
use function Async\timeout;

function abandon(Channel $channel, int $rounds): void
{
    for ($i = 0; $i < $rounds; $i++) {
        $future = $channel->recvAsync();
        $future->ignore();

        try {
            await_any_or_fail([$future], timeout(1));
        } catch (OperationCanceledException) {
        }
    }
}

$channel = new Channel(1);
abandon($channel, 100);
gc_collect_cycles();
$before = memory_get_usage();

abandon($channel, 10000);
gc_collect_cycles();
echo "memory grew: ", var_export(memory_get_usage() - $before > 1024, true), "\n";

$channel->sendAsync('value');
echo "count: ", count($channel), "\n";
echo "recv: ", $channel->recv(), "\n";
?>
--EXPECT--
memory grew: false
count: 1
recv: value
