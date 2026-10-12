--TEST--
Pool: while INACTIVE, tryAcquire() asks shouldRecover() too, and true serves it
--FILE--
<?php

use Async\CircuitBreakerStrategy;

class RecoveringStrategy implements CircuitBreakerStrategy
{
    public function reportSuccess(mixed $source): void {}

    public function reportFailure(mixed $source, \Throwable $error): void {}

    public function shouldRecover(): bool
    {
        echo "shouldRecover\n";
        return true;
    }
}

$pool = new Async\Pool(factory: fn() => 'r');
$pool->setCircuitBreakerStrategy(new RecoveringStrategy());
$pool->deactivate();

var_dump($pool->tryAcquire());
echo $pool->getState()->name, "\n";
?>
--EXPECT--
shouldRecover
string(1) "r"
RECOVERING
