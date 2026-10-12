--TEST--
spawn_with(): a hook that throws while the scheduler cannot get a stack throws the hook's exception, and the coroutine, which the cancel cannot queue, is discarded unrun
--FILE--
<?php
class RefusingStrategy implements Async\SpawnStrategy
{
    public function provideScope(): ?Async\Scope
    {
        return null;
    }

    public function beforeCoroutineEnqueue($coroutine, $scope): array
    {
        throw new Exception("hook refused");
    }

    public function afterCoroutineEnqueue($coroutine, $scope): void {}
}

ini_set('fiber.stack_size', '1048576G'); // 1 PiB: beyond what mmap and VirtualAlloc can place, refused everywhere

try {
    Async\spawn_with(new RefusingStrategy(), fn() => print("not reached\n"));
    echo "not reached\n";
} catch (Exception $exception) {
    echo "spawn_with: ", $exception->getMessage(), "\n";
}

ini_restore('fiber.stack_size');
var_dump(Async\get_coroutines() === [Async\current_coroutine()]);
?>
--EXPECT--
spawn_with: hook refused
bool(true)
