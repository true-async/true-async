--TEST--
TaskGroup: a spawn() whose coroutine cannot get a stack throws the refusal and adds no task; the next spawn() runs
--FILE--
<?php
$group = new Async\TaskGroup();

ini_set('fiber.stack_size', '1048576G'); // 1 PiB: beyond what mmap and VirtualAlloc can place, refused everywhere

try {
    $group->spawn(fn() => print("not reached\n"));
    echo "not reached\n";
} catch (Exception $exception) {
    echo "spawn: ", $exception->getMessage(), "\n";
}

ini_restore('fiber.stack_size');
var_dump($group->count());

$group->spawn(fn() => print("task ran\n"));
$group->close();
$group->awaitCompletion();
?>
--EXPECTF--
spawn: Fiber stack allocate failed: %s
int(0)
task ran
