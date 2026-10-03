--TEST--
A parked coroutine's context keeps its first VM stack page on its C stack, not in the request's memory (as TrueAsync's fiber_entry)
--INI--
memory_limit=8M
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;

const COUNT = 1000;

$before = memory_get_usage();
$coroutines = [];

for ($i = 0; $i < COUNT; $i++) {
    $coroutines[] = spawn(static function () {
        suspend();
    });
}

// Every coroutine runs up to its suspend() and parks on a context of its own.
suspend();
$per_coroutine = (memory_get_usage() - $before) / COUNT;

foreach ($coroutines as $coroutine) {
    await($coroutine);
}

// A 16 KiB page per context would take 16 MiB here, over the limit.
var_dump($per_coroutine < 4096);
echo "done\n";
?>
--EXPECT--
bool(true)
done
