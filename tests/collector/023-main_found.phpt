--TEST--
get_deadlocked_coroutines(): main awaiting a coroutine that awaits main, with nothing else holding either, is found
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\current_coroutine;
use function Async\get_deadlocked_coroutines;

final class Box
{
    public static ?Async\Coroutine $main = null;
    public static int $main_id = 0;
}

Box::$main = current_coroutine();
Box::$main_id = spl_object_id(Box::$main);

spawn(function () {
    for ($i = 0; $i < 4; $i++) {
        suspend();
    }

    $found = get_deadlocked_coroutines();
    $ids = array_map(spl_object_id(...), $found);
    echo count($found), " found, main among them: ", var_export(in_array(Box::$main_id, $ids, true), true), "\n";

    foreach ($found as $coroutine) {
        $coroutine->cancel();
    }
});

try {
    await(spawn(function () {
        $main = Box::$main;
        Box::$main = null;
        await($main);
    }));
} catch (Async\AsyncCancellation $cancellation) {
    echo "main: ", $cancellation->getMessage(), "\n";
}

echo "end\n";
?>
--EXPECT--
2 found, main among them: true
main: Coroutine cancelled
end
