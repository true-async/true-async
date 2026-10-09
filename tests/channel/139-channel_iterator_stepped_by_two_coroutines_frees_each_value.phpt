--TEST--
Channel: one iterator stepped by two coroutines at once frees each value it received
--FILE--
<?php

use Async\Channel;
use function Async\await_all;
use function Async\spawn;

final class Value
{
    public static int $freed = 0;

    public function __destruct()
    {
        self::$freed++;
    }
}

$channel = new Channel(4);
$channel->sendAsync(new Value());
$iterator = $channel->getIterator();
$iterator->rewind();
$started = 0;
$step = function () use ($iterator, &$started) {
    $started++;
    $iterator->next();
};
$first = spawn($step);
$second = spawn($step);

while ($started < 2) {
    Async\suspend();
}

$channel->sendAsync(new Value());
$channel->sendAsync(new Value());
await_all([$first, $second]);
echo "freed before the iterator's release: ", Value::$freed, "\n";
unset($iterator, $step, $first, $second);
echo "freed after it: ", Value::$freed, "\n";
?>
--EXPECT--
freed before the iterator's release: 2
freed after it: 3
