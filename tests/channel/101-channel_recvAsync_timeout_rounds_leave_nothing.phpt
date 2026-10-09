--TEST--
Channel: a recvAsync() Future abandoned by a Timeout is freed and leaves the queue
--FILE--
<?php

use Async\Channel;
use Async\OperationCanceledException;
use function Async\await_any_or_fail;
use function Async\timeout;

$channel = new Channel(1);
$future = $channel->recvAsync();
$future->ignore();

try {
    await_any_or_fail([$future], timeout(1));
} catch (OperationCanceledException) {
    echo "timed out\n";
}

$reference = WeakReference::create($future);
unset($future);
echo "freed: ", var_export($reference->get() === null, true), "\n";

$channel->sendAsync('value');
echo "count: ", count($channel), "\n";
echo "recv: ", $channel->recv(), "\n";
?>
--EXPECT--
timed out
freed: true
count: 1
recv: value
