--TEST--
Pool: a pre-warm factory that acquires from the pool it fills keeps the pool within max
--FILE--
<?php

use Async\Pool;

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
$taken = null;

$pool = new Pool(
    factory: function () use (&$calls, &$taken) {
        $calls++;

        if ($calls === 1) {
            $taken = pool_being_built()->acquire();
        }

        return new stdClass();
    },
    min: 2,
    max: 2,
);

printf("calls=%d idle=%d active=%d count=%d\n", $calls, $pool->idleCount(), $pool->activeCount(), count($pool));

$pool->release($taken);

printf("idle=%d active=%d count=%d\n", $pool->idleCount(), $pool->activeCount(), count($pool));
?>
--EXPECT--
calls=2 idle=1 active=1 count=2
idle=2 active=0 count=2
