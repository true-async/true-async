--TEST--
Future::__construct() again on a child whose drain item is queued: a destructor that suspends during the release finds the new state in place
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\await;

class SuspendsOnDestruct
{
    public function __destruct()
    {
        echo "destructor suspends\n";
        Async\suspend();
        echo "destructor resumed\n";
    }
}

$source = new FutureState();
$child = (new Future($source))->map(function ($value) {
    echo "mapper got $value\n";
    return $value + 1;
});
$child->ignore();

$first = new FutureState();
$child->__construct($first);
$first->ignore();
$first->complete(new SuspendsOnDestruct());
unset($first);

$source->complete(1);

$second = new FutureState();
$child->__construct($second);

var_dump($second->isCompleted());
var_dump(await($child));

?>
--EXPECT--
destructor suspends
mapper got 1
destructor resumed
bool(true)
int(2)
