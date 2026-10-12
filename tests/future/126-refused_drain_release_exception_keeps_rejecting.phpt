--TEST--
Future: a destructor that throws while a refused drain releases a mapped child does not stop the rejection: the children after it are rejected too, and its exception is thrown over the refusal
--FILE--
<?php
class ThrowsOnRelease
{
    public function __destruct()
    {
        throw new Exception("destructor");
    }
}

$state = new Async\FutureState();
$source = new Async\Future($state);
$held = new ThrowsOnRelease();
$source->map(function ($value) use ($held) {
    return $value;
})->ignore();
unset($held);
$second = $source->map(fn($value) => $value + 1);

ini_set('fiber.stack_size', '1048576G'); // 1 PiB: beyond what mmap and VirtualAlloc can place, refused everywhere

try {
    $state->complete(1);
    echo "not reached\n";
} catch (Exception $exception) {
    echo "complete: ", $exception->getMessage(), "\n";
    echo "previous: ", $exception->getPrevious()->getMessage(), "\n";
}

ini_restore('fiber.stack_size');

try {
    $second->await();
    echo "not reached\n";
} catch (Exception $exception) {
    echo "second: ", $exception->getMessage(), "\n";
}
?>
--EXPECTF--
complete: destructor
previous: Fiber stack allocate failed: %s
second: Fiber stack allocate failed: %s
