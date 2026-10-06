--TEST--
get_deadlocked_coroutines(): an await inside a generator; the generator's frame is not walked, so what only it holds counts as held from outside
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function waits(Async\Coroutine $target): Generator
{
    yield 1;
    await($target);
    yield 2;
}

function start_pair(): void
{
    $holder = new stdClass();
    $holder->a = spawn(function () use ($holder) {
        suspend();

        foreach (waits($holder->b) as $value) {
        }
    });
    $holder->b = spawn(function () use ($holder) {
        suspend();
        await($holder->a);
    });
}

start_pair();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

$found = get_deadlocked_coroutines();
echo count($found), " found\n";

foreach (Async\get_coroutines() as $coroutine) {
    if ($coroutine !== Async\current_coroutine()) {
        $coroutine->cancel();
    }
}

suspend();
echo "end\n";
?>
--EXPECT--
0 found
end
