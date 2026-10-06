--TEST--
Future: a cycle through the result of a FutureState that is the event's only holder is collected
--FILE--
<?php

use Async\FutureState;
use Async\Future;

class Holder
{
    public $state;
    public $future;

    public function __destruct()
    {
        echo "Holder destroyed\n";
    }
}

$holder = new Holder();
$holder->state = new FutureState();
$holder->future = new Future($holder->state);
$holder->future->ignore();
$holder->state->complete($holder);
unset($holder);

$collected = gc_collect_cycles();
echo "collected: ", $collected > 0 ? "yes" : "no", "\n";
echo "done\n";

?>
--EXPECT--
Holder destroyed
collected: yes
done
