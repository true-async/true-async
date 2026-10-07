--TEST--
The GC reaches every child of a Future's chain: three children whose mappers hold the parent are all collected
--FILE--
<?php

use Async\FutureState;
use Async\Future;

$state = new FutureState();
$parent = new Future($state);
$children = [];

for ($i = 0; $i < 3; $i++) {
    $child = $parent->map(function () use (&$parent) {
        return $parent;
    });
    $child->ignore();
    $children[] = WeakReference::create($child);
}

$state->ignore();
unset($parent, $state, $child);
gc_collect_cycles();

foreach ($children as $i => $child) {
    echo $i, ": ", $child->get() === null ? "collected" : "alive", "\n";
}

?>
--EXPECT--
0: collected
1: collected
2: collected
