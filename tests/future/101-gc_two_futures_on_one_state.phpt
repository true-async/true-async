--TEST--
Future: two Futures on one state inside a cycle through the result; a collection frees nothing a live Future still reads
--FILE--
<?php

use Async\FutureState;
use Async\Future;

class Node
{
    public $state;
    public $first;
    public $second;
    public $value = "kept";
}

$state = new FutureState();
$first = new Future($state);
$second = new Future($state);
$node = new Node();
$node->state = $state;
$node->first = $first;
$node->second = $second;
$state->complete($node);
unset($second, $node, $state);

var_dump(gc_collect_cycles());
echo $first->await()->value, "\n";
echo $first->await()->second->await()->value, "\n";

?>
--EXPECT--
int(0)
kept
kept
