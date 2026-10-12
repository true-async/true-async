--TEST--
Pool: a strategy suspended in reportSuccess() keeps the released resource counted, so no other coroutine makes one past max
--FILE--
<?php

use Async\CircuitBreakerStrategy;
use function Async\spawn;
use function Async\await;
use function Async\suspend;

class SuspendingStrategy implements CircuitBreakerStrategy
{
    public bool $inReport = false;
    public bool $leave = false;

    public function reportSuccess(mixed $source): void
    {
        $this->inReport = true;
        while (!$this->leave) {
            suspend();
        }
    }

    public function reportFailure(mixed $source, \Throwable $error): void {}

    public function shouldRecover(): bool
    {
        return true;
    }
}

$created = 0;
$pool = new Async\Pool(factory: function () use (&$created) { return ++$created; }, max: 1);
$strategy = new SuspendingStrategy();
$pool->setCircuitBreakerStrategy($strategy);

$resource = $pool->tryAcquire();
$releaser = spawn(fn() => $pool->release($resource));

while (!$strategy->inReport) {
    suspend();
}

var_dump($pool->tryAcquire());
printf("count=%d created=%d\n", count($pool), $created);

$strategy->leave = true;
await($releaser);
printf("idle=%d active=%d\n", $pool->idleCount(), $pool->activeCount());
?>
--EXPECT--
NULL
count=1 created=1
idle=1 active=0
