--TEST--
await_all() over a Traversable whose iterator coroutine cannot get a stack throws the refusal before the Traversable is read
--FILE--
<?php
function triggers()
{
    echo "generator ran\n";
    yield Async\spawn(fn() => 1);
}

ini_set('fiber.stack_size', '1048576G'); // 1 PiB: beyond what mmap and VirtualAlloc can place, refused everywhere

try {
    Async\await_all(triggers());
    echo "not reached\n";
} catch (Exception $exception) {
    echo "await_all: ", $exception->getMessage(), "\n";
}

ini_restore('fiber.stack_size');
echo "end\n";
?>
--EXPECTF--
await_all: Fiber stack allocate failed: %s
end
