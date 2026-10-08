--TEST--
Channel: send() and recv() throw in scheduler context; sendAsync() works there
--FILE--
<?php

use Async\Channel;
use TrueAsync\Test;

$channel = new Channel(1);

Test\defer('A', null, function () use ($channel) {
    $calls = ['send' => fn() => $channel->send(1), 'recv' => fn() => $channel->recv()];

    foreach ($calls as $name => $call) {
        try {
            $call();
            echo "$name: no throw\n";
        } catch (Error $error) {
            echo "$name: ", $error->getMessage(), "\n";
        }
    }

    echo "sendAsync: ", var_export($channel->sendAsync(2), true), "\n";
});

Async\suspend();
echo "count: ", count($channel), "\n";
?>
--EXPECT--
microtask A sched=1
send: The operation cannot be executed in the scheduler context
recv: The operation cannot be executed in the scheduler context
sendAsync: true
released A
count: 1
