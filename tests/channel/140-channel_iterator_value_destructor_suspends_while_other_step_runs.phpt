--TEST--
Channel: a released iterator value whose destructor suspends is freed once while another coroutine steps the same iterator
--FILE--
<?php

use Async\Channel;
use function Async\await_all;
use function Async\delay;
use function Async\spawn;

final class Value
{
    public static int $freed = 0;
    public static bool $in_destructor = false;

    public function __construct(public bool $suspends)
    {
    }

    public function __destruct()
    {
        if ($this->suspends) {
            self::$in_destructor = true;
            delay(10);
        }

        self::$freed++;
    }
}

$channel = new Channel(4);
$channel->sendAsync(new Value(true));
$iterator = $channel->getIterator();
$iterator->rewind();
$step = function () use ($iterator) {
    $iterator->next();
};
$first = spawn($step);

while (!Value::$in_destructor) {
    Async\suspend();
}

$second = spawn($step);
$channel->sendAsync(new Value(false));
$channel->sendAsync(new Value(false));
await_all([$first, $second]);
unset($iterator, $step, $first, $second);
echo "freed: ", Value::$freed, "\n";
?>
--EXPECT--
freed: 3
