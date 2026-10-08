--TEST--
Channel: foreach throws in scheduler context; recvAsync() works there
--FILE--
<?php

use Async\Channel;
use TrueAsync\Test;

$channel = new Channel(1);
$channel->sendAsync(1);

Test\defer('A', null, function () use ($channel) {
    try {
        foreach ($channel as $value) {
            echo "value: $value\n";
        }
    } catch (Error $error) {
        echo "foreach: ", $error->getMessage(), "\n";
    }

    $future = $channel->recvAsync();
    echo "recvAsync completed: ", var_export($future->isCompleted(), true), "\n";
    $future->ignore();
});

Async\suspend();
echo "count: ", count($channel), "\n";
?>
--EXPECT--
microtask A sched=1
foreach: The operation cannot be executed in the scheduler context
recvAsync completed: true
released A
count: 0
