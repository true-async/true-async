--TEST--
GC 018: main waits for the run of each full buffer, and the threshold stays at the default
--INI--
zend.enable_gc=1
--FILE--
<?php

// The top-level script runs as the main coroutine from its start, so a full
// buffer makes it wait for the GC coroutine's run, as any coroutine does
// (DECISIONS 2026-10-02); the stream read changes nothing here. The spawned
// coroutine runs while main waits, so its line comes first; an inline
// collection would leave it for the end of the script.
file_get_contents(__FILE__);

Async\spawn(function () {
    echo "spawned ran while main waited\n";
});

$before = gc_status()['threshold'];

for ($i = 1; $i <= 40000; $i++) {
    $a = new stdClass();
    $b = new stdClass();
    $a->b = $b;
    $b->a = $a;
    unset($a, $b);
}

$status = gc_status();

// Every run frees far more cycles than PHP's trigger, so none of them raises
// the threshold.
echo "threshold before: $before\n";
echo "threshold after: ", $status['threshold'], "\n";
echo "runs: ", $status['runs'], "\n";

?>
--EXPECT--
spawned ran while main waited
threshold before: 10001
threshold after: 10001
runs: 8
