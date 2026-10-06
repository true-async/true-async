--TEST--
Future::map() - a mapper that calls __construct() on its own child does not free the event the drain completes
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\suspend;

$state = new FutureState();
$future = new Future($state);

$child = $future->map(function ($value) use (&$child) {
    $replacement = new FutureState();
    $replacement->ignore();
    $child->__construct($replacement);
    return 1;
});

$state->complete(0);
suspend();

var_dump($child->isCompleted());
echo "done\n";

?>
--EXPECTF--
Warning: Future was never used; call await(), map(), catch(), finally() or ignore() to suppress this warning. Created at %s:%d in %s on line %d
bool(false)
done
