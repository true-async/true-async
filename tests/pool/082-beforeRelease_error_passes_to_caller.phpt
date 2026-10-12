--TEST--
Pool: an Error thrown by beforeRelease destroys the resource, passes to the caller of release() and reports nothing to the strategy
--FILE--
<?php

use Async\CircuitBreakerStrategy;

class ReportingStrategy implements CircuitBreakerStrategy
{
    public function reportSuccess(mixed $source): void
    {
        echo "reportSuccess\n";
    }

    public function reportFailure(mixed $source, \Throwable $error): void
    {
        echo "reportFailure\n";
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
    beforeRelease: fn($resource) => $resource->rollback(),
);
$pool->setCircuitBreakerStrategy(new ReportingStrategy());

$resource = $pool->tryAcquire();

try {
    $pool->release($resource);
    echo "released\n";
} catch (Error $e) {
    echo $e::class, ": ", $e->getMessage(), "\n";
}

printf("count=%d\n", count($pool));
?>
--EXPECT--
destroyed r
Error: Call to a member function rollback() on string
count=0
