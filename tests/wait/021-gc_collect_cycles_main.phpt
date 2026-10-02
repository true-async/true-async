--TEST--
S3.7 item 13: gc_collect_cycles() in main returns after the destructors ran, with the count of collected objects
--FILE--
<?php

class Node {
    public ?Node $other = null;

    public function __construct(public string $name) {}

    public function __destruct() {
        $GLOBALS['log'][] = "destructed " . $this->name;
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
array(2) {
  [0]=>
  string(12) "destructed a"
  [1]=>
  string(12) "destructed b"
}
int(2)
