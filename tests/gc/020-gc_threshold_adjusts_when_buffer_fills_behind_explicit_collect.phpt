--TEST--
GC 020: an explicit collection in a coroutine owes no threshold step, since nothing fills behind it
--INI--
zend.enable_gc=1
--FILE--
<?php

use function Async\await;
use function Async\delay;
use function Async\spawn;

$live = [];

// The explicit request waits for the GC coroutine's whole run, which empties
// the buffer (DECISIONS 2026-10-02). The later cycles do not fill it again, so
// no full-buffer step is owed and the threshold stays at the default.
await(spawn(function () use (&$live) {
    for ($i = 1; $i <= 4000; $i++) {
        $a = new stdClass();
        $b = new stdClass();
        $a->b = $b;
        $b->a = $a;
        $live[] = $a;
        unset($a, $b);
    }

    gc_collect_cycles();

    for ($i = 1; $i <= 1200; $i++) {
        $a = new stdClass();
        $b = new stdClass();
        $a->b = $b;
        $b->a = $a;
        $live[] = $a;
        unset($a, $b);
    }

    delay(1);
}));

echo "threshold: ", gc_status()['threshold'], "\n";

?>
--EXPECT--
threshold: 10001
