--TEST--
get_deadlocked_coroutines(): an object whose dynamic properties table an array cast shares, reached by a found pair and held by main, keeps its values and stays apart from the array after the walk
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start_pair(stdClass $object): void
{
    $a = null;
    $b = null;
    $a = spawn(function () use (&$b, $object) {
        suspend();
        await($b);
    });
    $b = spawn(function () use (&$a) {
        suspend();
        await($a);
    });
}

$object = new stdClass();
$object->first = 1;
$object->second = "two";
$cast = (array) $object;
start_pair($object);

for ($i = 0; $i < 4; $i++) {
    suspend();
}

$found = get_deadlocked_coroutines();
echo count($found), " found\n";
$object->first = 10;
$cast['second'] = "changed";
var_dump($object->first, $object->second, $cast['first'], $cast['second']);

foreach ($found as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECT--
2 found
int(10)
string(3) "two"
int(1)
string(7) "changed"
