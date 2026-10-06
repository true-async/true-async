--TEST--
get_deadlocked_coroutines(): the returned coroutines are reachable while held, so a second call finds none, and they can be cancelled
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start_pair(): void
{
    $a = null;
    $b = null;
    $a = spawn(function () use (&$b) {
        try {
            suspend();
            await($b);
        } finally {
            echo "a unwinds\n";
        }
    });
    $b = spawn(function () use (&$a) {
        try {
            suspend();
            await($a);
        } finally {
            echo "b unwinds\n";
        }
    });
}

start_pair();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

$found = get_deadlocked_coroutines();
echo count($found), " found\n";
echo count(get_deadlocked_coroutines()), " found while held\n";

foreach ($found as $coroutine) {
    $coroutine->cancel();
}

unset($found);
suspend();
echo count(get_deadlocked_coroutines()), " found after the cancel\n";
?>
--EXPECT--
2 found
0 found while held
a unwinds
b unwinds
0 found after the cancel
