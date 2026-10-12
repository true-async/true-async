--TEST--
Pool: an Exception thrown by beforeRelease rejects the resource silently and reaches reportFailure() as the error
--FILE--
<?php

use Async\CircuitBreakerStrategy;

class ReportingStrategy implements CircuitBreakerStrategy
{
    public function reportSuccess(mixed $source): void {}

    public function reportFailure(mixed $source, \Throwable $error): void
    {
        echo "reportFailure: ", $error::class, ": ", $error->getMessage(), "\n";
    }

    public function shouldRecover(): bool
    {
        return true;
    }
}

$pool = new Async\Pool(
    factory: fn() => 'r',
    destructor: function ($resource) {
        echo "destroyed $resource\n";
    },
    beforeRelease: fn($resource) => throw new RuntimeException("connection lost"),
);
$pool->setCircuitBreakerStrategy(new ReportingStrategy());

$pool->release($pool->tryAcquire());
printf("count=%d\n", count($pool));
?>
--EXPECT--
reportFailure: RuntimeException: connection lost
destroyed r
count=0
