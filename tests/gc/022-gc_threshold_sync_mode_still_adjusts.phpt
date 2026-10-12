--TEST--
GC 022: the threshold still adjusts both ways, by the run a full buffer queues
--INI--
zend.enable_gc=1
--FILE--
<?php

// Main is a coroutine (DECISIONS 2026-10-02): a full buffer queues the run,
// gc_collect_cycles() waits for it, and the run adjusts from the real count -
// the pre-async contract, in both directions (DECISIONS 2026-10-12 S9.38).

$live = [];

// Live cycles fill the buffer: the run frees nothing, one step up.
for ($i = 1; $i <= 5100; $i++) {
    $a = new stdClass();
    $b = new stdClass();
    $a->b = $b;
    $b->a = $a;
    $live[] = $a;
    unset($a, $b);
}

gc_collect_cycles();
$raised = gc_status()['threshold'];

// The cycles become garbage and enough new garbage fills the raised buffer:
// the run collects everything, one step back down.
$live = null;
for ($i = 1; $i <= 7500; $i++) {
    $a = new stdClass();
    $b = new stdClass();
    $a->b = $b;
    $b->a = $a;
    unset($a, $b);
}

gc_collect_cycles();
$lowered = gc_status()['threshold'];

echo "after raise: $raised\n";
echo "after lower: $lowered\n";

?>
--EXPECT--
after raise: 20001
after lower: 10001
