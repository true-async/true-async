--TEST--
Pool: a constructor refused for its counts releases trampoline callables
--FILE--
<?php

use Async\Pool;

final class Callables
{
    public function __call(string $name, array $arguments): mixed
    {
        return 1;
    }
}

$callables = new Callables();

try {
    new Pool(
        factory: [$callables, 'make'],
        destructor: [$callables, 'destroy'],
        healthcheck: [$callables, 'check'],
        beforeAcquire: [$callables, 'before'],
        beforeRelease: [$callables, 'after'],
        min: 2,
        max: 1,
    );
} catch (ValueError $exception) {
    echo $exception->getMessage(), "\n";
}
?>
--EXPECT--
Async\Pool::__construct(): Argument #6 ($min) must be <= max
