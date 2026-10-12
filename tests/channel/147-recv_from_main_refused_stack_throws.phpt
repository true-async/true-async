--TEST--
Channel: recv() from main on an empty channel when the scheduler cannot get a stack throws the refusal and leaves no waiter; a later send() and recv() work
--FILE--
<?php
$channel = new Async\Channel(1);

ini_set('fiber.stack_size', '1048576G'); // 1 PiB: beyond what mmap and VirtualAlloc can place, refused everywhere

try {
    $channel->recv();
    echo "not reached\n";
} catch (Exception $exception) {
    echo "recv: ", $exception->getMessage(), "\n";
}

ini_restore('fiber.stack_size');
$channel->send(5);
var_dump($channel->recv());
?>
--EXPECTF--
recv: Fiber stack allocate failed: %s
int(5)
