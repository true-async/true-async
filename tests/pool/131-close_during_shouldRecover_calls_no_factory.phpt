--TEST--
Pool: a pool closed while shouldRecover() suspends calls no factory and acquire() finds it closed
--FILE--
<?php

use Async\CircuitBreakerStrategy;
use Async\Pool;
use function Async\await;
use function Async\spawn;

final class SlowRecovery implements CircuitBreakerStrategy
{
    public function __construct(private Async\FutureState $parked, private Async\FutureState $closed) {}

    public function reportSuccess(mixed $source): void {}

    public function reportFailure(mixed $source, Throwable $error): void {}

    public function shouldRecover(): bool
    {
        $this->parked->complete(null);
        await(new Async\Future($this->closed));
        return true;
    }
}

$pool = new Pool(
    factory: function () {
        echo "factory\n";
        return 1;
    },
    max: 1,
);
$parked = new Async\FutureState();
$closed = new Async\FutureState();
$pool->setCircuitBreakerStrategy(new SlowRecovery($parked, $closed));
$pool->deactivate();

$caller = spawn(function () use ($pool) {
    try {
        $pool->acquire();
    } catch (Async\PoolException $exception) {
        echo $exception->getMessage(), "\n";
    }
});

await(new Async\Future($parked));
$pool->close();
$closed->complete(null);
await($caller);
?>
--EXPECT--
Pool is closed
