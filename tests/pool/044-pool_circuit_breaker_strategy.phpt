--TEST--
Pool: circuit breaker - setCircuitBreakerStrategy
--FILE--
<?php

use Async\Pool;
use Async\CircuitBreakerStrategy;
use Async\CircuitBreakerState;

class TestStrategy implements CircuitBreakerStrategy
{
    public int $successCount = 0;
    public int $failureCount = 0;

    public function reportSuccess(mixed $source): void
    {
        $this->successCount++;
        echo "Strategy: success reported\n";
    }

    public function reportFailure(mixed $source, \Throwable $error): void
    {
        $this->failureCount++;
        echo "Strategy: failure reported - " . $error->getMessage() . "\n";
    }

    public function shouldRecover(): bool
    {
        return true;
    }
}

$strategy = new TestStrategy();

$pool = new Pool(
    factory: fn() => 42
);

// Set strategy
$pool->setCircuitBreakerStrategy($strategy);
echo "Strategy set\n";

$pool->release($pool->acquire());
echo "Success count: {$strategy->successCount}\n";

// Remove strategy
$pool->setCircuitBreakerStrategy(null);
echo "Strategy removed\n";

// A removed strategy must not be called any more.
$pool->release($pool->acquire());
echo "Success count: {$strategy->successCount}\n";

echo "Done\n";
?>
--EXPECT--
Strategy set
Strategy: success reported
Success count: 1
Strategy removed
Success count: 1
Done
