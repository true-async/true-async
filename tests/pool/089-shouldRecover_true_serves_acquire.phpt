--TEST--
Pool: while INACTIVE, acquire() asks shouldRecover(), and true moves the pool to RECOVERING and serves the call
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

echo $pool->acquire(), "\n";
echo $pool->getState()->name, "\n";
?>
--EXPECT--
shouldRecover
r
RECOVERING
