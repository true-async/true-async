--TEST--
Pool: a destructor cancelled after reportFailure() threw puts the cancellation over the strategy's exception
--FILE--
<?php

use Async\CircuitBreakerStrategy;
use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

final class ThrowingReport implements CircuitBreakerStrategy
{
    public function reportSuccess(mixed $source): void {}

    public function reportFailure(mixed $source, Throwable $error): void
    {
        throw new LogicException("strategy failed");
    }

    public function shouldRecover(): bool
    {
        return false;
    }
}

$parked = new Async\FutureState();
$pool = new Pool(
    factory: fn() => 1,
    destructor: function ($resource) use ($parked) {
        $parked->complete(null);
        delay(10000);
    },
    beforeRelease: fn($resource) => false,
);
$pool->setCircuitBreakerStrategy(new ThrowingReport());

$releaser = spawn(function () use ($pool) {
    try {
        $pool->release($pool->acquire());
    } catch (Exception $exception) {
        echo "caught by catch (Exception): ", $exception->getMessage(), "\n";
    }
    echo "not reached\n";
});

await(new Async\Future($parked));
$releaser->cancel();

try {
    await($releaser);
} catch (Async\AsyncCancellation $cancellation) {
    echo get_class($cancellation), " <- ", $cancellation->getPrevious()->getMessage(), "\n";
}
?>
--EXPECT--
Async\AsyncCancellation <- strategy failed
