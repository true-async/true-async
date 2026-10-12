--TEST--
Future: a source completed while no coroutine stack can be had runs no mapper; its mapped children and theirs are rejected with the refusal instead of staying pending
--FILE--
<?php
$state = new Async\FutureState();
$source = new Async\Future($state);
$child = $source->map(fn($value) => $value + 1);
$grandchild = $child->map(fn($value) => $value + 1);

ini_set('fiber.stack_size', '1048576G'); // 1 PiB: beyond what mmap and VirtualAlloc can place, refused everywhere

try {
    $state->complete(1);
    echo "not reached\n";
} catch (Exception $exception) {
    echo "complete: ", $exception->getMessage(), "\n";
}

ini_restore('fiber.stack_size');

foreach (['child' => $child, 'grandchild' => $grandchild] as $name => $future) {
    try {
        $future->await();
        echo "not reached\n";
    } catch (Exception $exception) {
        echo $name, ": ", $exception->getMessage(), "\n";
    }
}
?>
--EXPECTF--
complete: Fiber stack allocate failed: %s
child: Fiber stack allocate failed: %s
grandchild: Fiber stack allocate failed: %s
