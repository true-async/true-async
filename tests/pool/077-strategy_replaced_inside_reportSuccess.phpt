--TEST--
Pool: reportSuccess() that replaces its own strategy, the last reference to it, runs to its end
--FILE--
<?php

use Async\CircuitBreakerStrategy;

class ReplacedStrategy implements CircuitBreakerStrategy
{
    public string $name = 'first';

    public function reportSuccess(mixed $source): void
    {
        $source->setCircuitBreakerStrategy(null);
        echo "reportSuccess of {$this->name} after its replacement\n";
    }

    public function reportFailure(mixed $source, \Throwable $error): void {}

    public function shouldRecover(): bool
    {
        return true;
    }

    public function __destruct()
    {
        echo "strategy destroyed\n";
    }
}

$pool = new Async\Pool(factory: fn() => 'r');
$pool->setCircuitBreakerStrategy(new ReplacedStrategy());

$pool->release($pool->tryAcquire());
printf("idle=%d\n", $pool->idleCount());
?>
--EXPECT--
reportSuccess of first after its replacement
strategy destroyed
idle=1
