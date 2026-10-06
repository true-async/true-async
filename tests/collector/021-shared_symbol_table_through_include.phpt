--TEST--
get_deadlocked_coroutines(): an include inside a function shares the function's symbol table; the awaited coroutine, also kept in a static property, is not reported
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

final class Registry
{
    public static ?Async\Coroutine $target = null;
}

function await_through_include(): void
{
    $target = Registry::$target;
    include __DIR__ . '/021-shared_symbol_table_through_include.inc';
}

function start(): void
{
    $holder = new stdClass();
    $holder->waiter = spawn(function () {
        suspend();
        await_through_include();
    });
    Registry::$target = spawn(function () use ($holder) {
        suspend();
        await($holder->waiter);
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";
Registry::$target->cancel();
suspend();
suspend();
echo "end\n";
?>
--EXPECT--
0 found
end
