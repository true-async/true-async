--TEST--
GC 023: a run that finds the suspected objects still alive raises the threshold as PHP does
--INI--
zend.enable_gc=1
--FILE--
<?php

use function Async\await;
use function Async\delay;
use function Async\spawn;

// The buffer fills with objects that are alive but suspected of cycles. The
// coroutine waits for the GC coroutine's run (DECISIONS 2026-10-02), which finds
// them alive and collects nothing, so the threshold takes PHP's step. Released
// afterwards, they leave no roots behind.
await(spawn(function () {
    $keep = [];

    for ($i = 1; $i <= 11000; $i++) {
        $o = new stdClass();
        $keep[$i] = $o;
        unset($o);
    }

    $keep = null;

    delay(1);
}));

$status = gc_status();

echo "roots: ", $status['roots'], "\n";
echo "collected: ", $status['collected'], "\n";
echo "threshold: ", $status['threshold'], "\n";

?>
--EXPECT--
roots: 0
collected: 0
threshold: 20001
