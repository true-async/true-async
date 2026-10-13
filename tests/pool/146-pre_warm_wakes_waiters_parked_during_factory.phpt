--TEST--
Pool: waiters parked while a pre-warm factory suspends are each woken for a place, and the pre-warm stops at the promised ones
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

function pool_being_built(): Pool
{
    foreach (debug_backtrace(DEBUG_BACKTRACE_PROVIDE_OBJECT) as $frame) {
        if (($frame['object'] ?? null) instanceof Pool) {
            return $frame['object'];
        }
    }

    throw new LogicException('no pool in the backtrace');
}

$calls = 0;
$waiters = [];

$pool = new Pool(
    factory: function () use (&$calls, &$waiters) {
        $number = ++$calls;
        echo "factory $number\n";

        if ($number === 1) {
            $pool = pool_being_built();

            // Takes the pool's other place, parks two waiters and frees the place by a rejected release.
            $holder = spawn(function () use ($pool, &$waiters) {
                $taken = $pool->acquire();

                foreach (['first', 'second'] as $name) {
                    $waiters[$name] = spawn(function () use ($pool, $name) {
                        $resource = $pool->acquire();
                        echo "$name got $resource\n";
                    });
                }

                while (!$waiters['first']->isStarted() || !$waiters['second']->isStarted()) {
                    suspend();
                }

                $pool->release($taken);
            });

            while (!$holder->isCompleted()) {
                suspend();
            }
        }

        return "resource $number";
    },
    beforeRelease: fn(string $resource) => false,
    min: 2,
    max: 2,
);

printf("constructed: idle=%d active=%d\n", $pool->idleCount(), $pool->activeCount());

foreach ($waiters as $waiter) {
    await($waiter);
}

printf("calls=%d idle=%d active=%d\n", $calls, $pool->idleCount(), $pool->activeCount());
?>
--EXPECT--
factory 1
factory 2
constructed: idle=1 active=0
first got resource 1
factory 3
second got resource 3
calls=3 idle=0 active=2
