--TEST--
TaskGroup: five reads pending at once are all answered
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$gate = new FutureState();
$group = new TaskGroup();
$group->spawn(fn() => (new Future($gate))->await());

$reads = [];
for ($i = 0; $i < 5; $i++) {
    $reads[] = $group->race();
}

$gate->complete("done");
foreach ($reads as $i => $read) {
    echo "$i: ", $read->await(), "\n";
}
$group->close();
?>
--EXPECT--
0: done
1: done
2: done
3: done
4: done
