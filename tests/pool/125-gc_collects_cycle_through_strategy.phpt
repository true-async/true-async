--TEST--
Pool: a cycle through the circuit breaker strategy is collected
--FILE--
<?php

use Async\CircuitBreakerStrategy;
use Async\Pool;

final class Strategy implements CircuitBreakerStrategy
{
    public ?Pool $pool = null;

    public function reportSuccess(mixed $source): void {}

    public function reportFailure(mixed $source, Throwable $error): void {}

    public function shouldRecover(): bool
    {
        return false;
    }
}

$strategy = new Strategy();
$strategy->pool = new Pool(factory: fn() => 1);
$strategy->pool->setCircuitBreakerStrategy($strategy);
$watch = WeakReference::create($strategy);
unset($strategy);
gc_collect_cycles();

echo "strategy alive: ", $watch->get() === null ? "no" : "yes", "\n";
?>
--EXPECT--
strategy alive: no
