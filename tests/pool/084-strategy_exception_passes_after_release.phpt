--TEST--
Pool: an exception from reportSuccess() passes to the caller of release() once the resource is back in the pool
--FILE--
<?php

use Async\CircuitBreakerStrategy;

class ThrowingStrategy implements CircuitBreakerStrategy
{
    public function reportSuccess(mixed $source): void
    {
        throw new RuntimeException("strategy failed");
    }

    public function reportFailure(mixed $source, \Throwable $error): void {}

    public function shouldRecover(): bool
    {
        return true;
    }
}

$pool = new Async\Pool(factory: fn() => 'r');
$pool->setCircuitBreakerStrategy(new ThrowingStrategy());

$resource = $pool->tryAcquire();

try {
    $pool->release($resource);
    echo "released\n";
} catch (RuntimeException $e) {
    echo $e->getMessage(), "\n";
}

printf("idle=%d active=%d\n", $pool->idleCount(), $pool->activeCount());
?>
--EXPECT--
strategy failed
idle=1 active=0
