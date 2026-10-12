--TEST--
Future::map() on a completed source while no coroutine stack can be had throws the refusal, and the child it never returned reports nothing more
--FILE--
<?php
$state = new Async\FutureState();
$source = new Async\Future($state);
$state->complete(1);

ini_set('fiber.stack_size', '1048576G'); // 1 PiB: beyond what mmap and VirtualAlloc can place, refused everywhere

try {
    $source->map(fn($value) => $value + 1);
    echo "not reached\n";
} catch (Exception $exception) {
    echo "map: ", $exception->getMessage(), "\n";
}

ini_restore('fiber.stack_size');
echo "end\n";
?>
--EXPECTF--
map: Fiber stack allocate failed: %s
end
