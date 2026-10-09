--TEST--
TaskGroup: a pending read freed before the answer leaves the reads before and after it answered
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$gate = new FutureState();
$group = new TaskGroup();
$group->spawn(fn() => (new Future($gate))->await());

$first = $group->race();
$second = $group->race();
$third = $group->race();
$second->ignore();
unset($second);

$gate->complete("done");
echo $first->await(), " ", $third->await(), "\n";
$group->close();
?>
--EXPECT--
done done
