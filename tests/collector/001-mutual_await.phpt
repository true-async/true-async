--TEST--
get_deadlocked_coroutines(): two coroutines awaiting each other after their spawner dropped them
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
        suspend();
        await($b);
    });
    $b = spawn(function () use (&$a) {
        suspend();
        await($a);
    });
}

start_pair();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

$found = get_deadlocked_coroutines();

foreach ($found as $coroutine) {
    echo "found, parked at line ", $coroutine->getSuspendFileAndLine()[1], "\n";
}

foreach ($found as $coroutine) {
    $coroutine->cancel();
}

echo "end\n";
?>
--EXPECT--
found, parked at line 13
found, parked at line 17
end
