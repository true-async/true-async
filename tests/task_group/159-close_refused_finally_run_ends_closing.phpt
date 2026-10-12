--TEST--
TaskGroup: a close() whose finally run cannot get a coroutine stack throws the refusal, ends the closing and runs no handler
--FILE--
<?php
$group = new Async\TaskGroup();
$group->finally(function () {
    echo "not reached\n";
});

ini_set('fiber.stack_size', '1048576G'); // 1 PiB: beyond what mmap and VirtualAlloc can place, refused everywhere

try {
    $group->close();
    echo "not reached\n";
} catch (Exception $exception) {
    echo "close: ", $exception->getMessage(), "\n";
}

ini_restore('fiber.stack_size');
var_dump($group->isClosed(), $group->isFinished());
?>
--EXPECTF--
close: Fiber stack allocate failed: %s
bool(true)
bool(true)
