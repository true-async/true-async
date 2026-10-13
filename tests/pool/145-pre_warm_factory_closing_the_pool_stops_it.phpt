--TEST--
Pool: a pre-warm factory that closes the pool it fills stops the pre-warm, and its resource is destroyed
--FILE--
<?php

use Async\Pool;

$calls = 0;

$pool = new Pool(
    factory: function () use (&$calls) {
        $calls++;

        if ($calls === 2) {
            foreach (debug_backtrace(DEBUG_BACKTRACE_PROVIDE_OBJECT) as $frame) {
                if (($frame['object'] ?? null) instanceof Pool) {
                    $frame['object']->close();
                }
            }
        }

        return "resource $calls";
    },
    destructor: function (string $resource) {
        echo "destroyed $resource\n";
    },
    min: 3,
    max: 3,
);

printf("calls=%d closed=%d idle=%d active=%d\n", $calls, $pool->isClosed(), $pool->idleCount(), $pool->activeCount());
?>
--EXPECT--
destroyed resource 1
destroyed resource 2
calls=2 closed=1 idle=0 active=0
