--TEST--
Pool: release() after a suspending reportSuccess() keeps the caller's cancellation over the destructor's exception
--FILE--
<?php

use Async\CircuitBreakerStrategy;
use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

final class SlowReport implements CircuitBreakerStrategy
{
    public function __construct(private Async\FutureState $parked) {}

    public function reportSuccess(mixed $source): void
    {
        $this->parked->complete(null);
        delay(10000);
    }

    public function reportFailure(mixed $source, Throwable $error): void {}

    public function shouldRecover(): bool
    {
        return false;
    }
}

$pool = new Pool(
    factory: fn() => 1,
    destructor: function ($resource) {
        throw new RuntimeException("close of $resource failed");
    },
);
$parked = new Async\FutureState();
$pool->setCircuitBreakerStrategy(new SlowReport($parked));

$releaser = spawn(function () use ($pool) {
    $pool->release($pool->acquire());
    echo "not reached\n";
});

await(new Async\Future($parked));
$pool->close();
$releaser->cancel();

try {
    await($releaser);
} catch (Async\AsyncCancellation $cancellation) {
    echo get_class($cancellation), " <- ", $cancellation->getPrevious()->getMessage(), "\n";
}
?>
--EXPECT--
Async\AsyncCancellation <- close of 1 failed
