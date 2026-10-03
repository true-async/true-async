--TEST--
A core coroutine the scheduler refused to enqueue is no waiter: no deadlock, not listed
--FILE--
<?php
ini_set('fiber.stack_size', '64G'); // mmap refuses it (vm.overcommit_memory 0 or 2)

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
