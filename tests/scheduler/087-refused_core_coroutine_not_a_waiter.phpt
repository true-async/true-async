--TEST--
A core coroutine the scheduler refused to enqueue is no waiter: no deadlock, not listed
--FILE--
<?php
ini_set('fiber.stack_size', '1048576G'); // 1 PiB: beyond what mmap and VirtualAlloc can place, refused everywhere

$fiber = new Fiber(function () {
    echo "body\n";
});

try {
    $fiber->start();
} catch (Exception $e) {
    echo "refused\n";
}

var_dump(count(Async\get_coroutines()));

ini_restore('fiber.stack_size');
$fiber->start();
echo "end\n";
?>
--EXPECT--
refused
int(1)
body
end
