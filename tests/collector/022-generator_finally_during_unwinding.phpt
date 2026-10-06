--TEST--
get_deadlocked_coroutines(): a coroutine parked in a generator's finally while its caller unwinds an exception is read without a crash and not reported
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\current_coroutine;
use function Async\get_deadlocked_coroutines;

function awaits_in_finally(stdClass $holder): Generator
{
    try {
        yield 1;
    } finally {
        await($holder->other);
    }
}

function started(Generator $generator): Generator
{
    $generator->current();

    return $generator;
}

function take(Generator $generator, int $value): void
{
}

function fail(): int
{
    throw new Exception("unwinding");
}

/* The unfinished call to take() is released while the frame handles the exception. */
function throws_with_generator(stdClass $holder): void
{
    take(started(awaits_in_finally($holder)), fail());
}

function start(): void
{
    $holder = new stdClass();
    $holder->thrower = spawn(function () use ($holder) {
        suspend();
        throws_with_generator($holder);
    });
    $holder->other = spawn(function () use ($holder) {
        suspend();
        await($holder->thrower);
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";

foreach (Async\get_coroutines() as $coroutine) {
    if ($coroutine !== current_coroutine()) {
        $coroutine->cancel();
    }
}

suspend();
echo "end\n";
?>
--EXPECT--
0 found
end
