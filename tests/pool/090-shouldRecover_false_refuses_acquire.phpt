--TEST--
Pool: while INACTIVE, a shouldRecover() that says false leaves acquire() refused
--FILE--
<?php

use Async\CircuitBreakerStrategy;

class DownStrategy implements CircuitBreakerStrategy
{
    public function reportSuccess(mixed $source): void {}

    public function reportFailure(mixed $source, \Throwable $error): void {}

    public function shouldRecover(): bool
    {
        echo "shouldRecover\n";
        return false;
    }
}

$pool = new Async\Pool(factory: fn() => 'r');
$pool->setCircuitBreakerStrategy(new DownStrategy());
$pool->deactivate();

try {
    $pool->acquire();
    echo "acquired\n";
} catch (Async\ServiceUnavailableException $e) {
    echo $e->getMessage(), "\n";
}

echo $pool->getState()->name, "\n";
?>
--EXPECT--
shouldRecover
Service is unavailable (circuit breaker is open)
INACTIVE
