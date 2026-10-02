--TEST--
S3.7 item 13: gc_collect_cycles() in main returns only after destructors that suspend have finished
--FILE--
<?php

use function Async\suspend;

class Node {
    public ?Node $other = null;

    public function __construct(public string $name) {}

    public function __destruct() {
        $GLOBALS['log'][] = "started " . $this->name;
        suspend();
        $GLOBALS['log'][] = "finished " . $this->name;
    }
}

function make_cycle(): void {
    $a = new Node("a");
    $b = new Node("b");
    $a->other = $b;
    $b->other = $a;
}

$log = [];
gc_collect_cycles();

make_cycle();
$collected = gc_collect_cycles();

// The destructor order is not specified, so the log is sorted after gc_collect_cycles() returned.
sort($log);
var_dump($log);
// Two objects in one cycle: 2 (spec item 13).
var_dump($collected);
?>
--EXPECT--
array(4) {
  [0]=>
  string(10) "finished a"
  [1]=>
  string(10) "finished b"
  [2]=>
  string(9) "started a"
  [3]=>
  string(9) "started b"
}
int(2)
