--TEST--
get_deadlocked_coroutines(): literal arrays and strings shared by the compiled script on the parked stacks change nothing, and the pair is found
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

const NAMES = ['a', 'b'];

function start_pair(): void
{
    $a = null;
    $b = null;
    $a = spawn(function () use (&$b) {
        $list = [1, 2, 3];
        $names = NAMES;
        $text = 'literal';
        suspend();
        await($b);
    });
    $b = spawn(function () use (&$a) {
        $list = [1, 2, 3];
        suspend();
        await($a);
    });
}

start_pair();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

$found = get_deadlocked_coroutines();
echo count($found), " found\n";

foreach ($found as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECT--
2 found
