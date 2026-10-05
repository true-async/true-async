--TEST--
The call_on_main_stack slot runs its callback on the OS thread stack: in main directly, from a coroutine or a Fiber below main's suspension point, below the engine's context after main finished, and directly once the core turned async off
--SKIPIF--
<?php
if (!function_exists('TrueAsync\Test\call_on_main_stack')) die('skip the core does not know the OS stack bounds');
if (PHP_OS_FAMILY !== 'Linux' || !in_array(php_uname('m'), ['x86_64', 'aarch64'], true)) {
    die('skip the stack switch is for x86-64 and AArch64; the probe finds stacks by position on Linux only');
}
?>
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use TrueAsync\Test;

function probe(string $where): string {
    static $os_stack_base = null;
    $bases = Test\call_on_main_stack();
    $os_stack_base ??= $bases['caller'];

    return sprintf("%s: caller %s, callback %s\n", $where,
        $bases['caller'] === $os_stack_base ? 'on OS stack' : 'off OS stack',
        $bases['callback'] === $os_stack_base ? 'on OS stack' : 'off OS stack');
}

// Each level is an internal call (array_map), so main parks deeper in its C stack than before.
function await_deep(int $depth, Async\Coroutine $coroutine): string {
    return $depth === 0 ? await($coroutine) : array_map(fn() => await_deep($depth - 1, $coroutine), [0])[0];
}

// The first probe runs in main, on the OS thread stack.
echo probe("main");

echo await(spawn(function () {
    echo probe("coroutine");
    suspend();
    echo probe("coroutine after suspend");

    return "coroutine done\n";
}));

echo await_deep(100, spawn(fn() => probe("coroutine while main is deep")));

await(spawn(function () {
    $fiber = new Fiber(function () {
        echo probe("fiber");
        Fiber::suspend();
        echo probe("fiber resumed");
    });
    $fiber->start();
    $fiber->resume();
}));

// The output buffers end after the core turned async off: no current coroutine.
ob_start(fn(string $buffer) => $buffer . probe("output handler after async off"));

// Runs after main finished, while the scheduler drains the queue.
spawn(fn() => print(probe("coroutine after main")));
echo "main end\n";
?>
--EXPECT--
main: caller on OS stack, callback on OS stack
coroutine: caller off OS stack, callback on OS stack
coroutine after suspend: caller off OS stack, callback on OS stack
coroutine done
coroutine while main is deep: caller off OS stack, callback on OS stack
fiber: caller off OS stack, callback on OS stack
fiber resumed: caller off OS stack, callback on OS stack
main end
coroutine after main: caller off OS stack, callback on OS stack
output handler after async off: caller on OS stack, callback on OS stack
