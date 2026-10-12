--TEST--
Pool: an exception from shouldRecover() passes to the caller of acquire() and the pool stays INACTIVE
--FILE--
<?php

use Async\CircuitBreakerStrategy;

class BrokenStrategy implements CircuitBreakerStrategy
{
    public function reportSuccess(mixed $source): void {}

    public function reportFailure(mixed $source, \Throwable $error): void {}

    public function shouldRecover(): bool
    {
        throw new RuntimeException("strategy failed");
    }
}

$pool = new Async\Pool(factory: fn() => 'r');
$pool->setCircuitBreakerStrategy(new BrokenStrategy());
$pool->deactivate();

try {
    $pool->acquire();
    echo "acquired\n";
} catch (RuntimeException $e) {
    echo $e->getMessage(), "\n";
}

printf("%s count=%d\n", $pool->getState()->name, count($pool));
?>
--EXPECT--
strategy failed
INACTIVE count=0
