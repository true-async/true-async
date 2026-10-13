--TEST--
Pool: a pool closed while shouldRecover() suspends and then refuses throws "Pool is closed"
--FILE--
<?php

use Async\CircuitBreakerStrategy;
use Async\Pool;
use function Async\await;
use function Async\spawn;

final class SlowRefusal implements CircuitBreakerStrategy
{
    public function __construct(private Async\FutureState $parked, private Async\FutureState $closed) {}

    public function reportSuccess(mixed $source): void {}

    public function reportFailure(mixed $source, Throwable $error): void {}

    public function shouldRecover(): bool
    {
        $this->parked->complete(null);
        await(new Async\Future($this->closed));
        return false;
    }
}

$pool = new Pool(factory: fn() => 1, max: 1);
$parked = new Async\FutureState();
$closed = new Async\FutureState();
$pool->setCircuitBreakerStrategy(new SlowRefusal($parked, $closed));
$pool->deactivate();

$caller = spawn(function () use ($pool) {
    try {
        $pool->acquire();
    } catch (Async\AsyncException $exception) {
        echo get_class($exception), ": ", $exception->getMessage(), "\n";
    }
});

await(new Async\Future($parked));
$pool->close();
$closed->complete(null);
await($caller);
?>
--EXPECT--
Async\PoolException: Pool is closed
