--TEST--
TaskGroup: a destructor whose finally run cannot get a coroutine stack throws the refusal from the release, ends the closing and runs no handler
--FILE--
<?php
$group = new Async\TaskGroup();
$group->finally(function () {
    echo "not reached\n";
});

ini_set('fiber.stack_size', '1048576G'); // 1 PiB: beyond what mmap and VirtualAlloc can place, refused everywhere

try {
    unset($group);
    echo "not reached\n";
} catch (Exception $exception) {
    echo "release: ", $exception->getMessage(), "\n";
}

ini_restore('fiber.stack_size');
echo "end\n";
?>
--EXPECTF--
release: Fiber stack allocate failed: %s
end
