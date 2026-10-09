--TEST--
Channel: foreach leaves the next value in the channel when the previous value, released by the loop body, throws in its destructor
--FILE--
<?php

use Async\Channel;

final class Job
{
    public function __construct(public int $number)
    {
    }

    public function __destruct()
    {
        if ($this->number === 1) {
            throw new LogicException("destructor");
        }
    }
}

$channel = new Channel(4);
$channel->send(new Job(1));
$channel->send(new Job(2));

try {
    foreach ($channel as $job) {
        echo "got ", $job->number, "\n";
        unset($job);
    }
} catch (LogicException $exception) {
    echo "caught ", $exception->getMessage(), "\n";
}

echo "left: ", count($channel), "\n";
?>
--EXPECT--
got 1
caught destructor
left: 1
