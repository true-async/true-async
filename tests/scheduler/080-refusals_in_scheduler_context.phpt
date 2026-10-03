--TEST--
In scheduler context (a microtask) every Async\ function that needs a coroutine refuses before it acts
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;

$target = spawn(fn() => "target");

Test\defer('A', null, function () use ($target) {
    $calls = [
        'spawn' => fn() => Async\spawn(fn() => print("not reached\n")),
        'await' => fn() => Async\await($target),
        'suspend' => fn() => Async\suspend(),
        'current_coroutine' => fn() => Async\current_coroutine(),
        'get_coroutines' => fn() => Async\get_coroutines(),
        'graceful_shutdown' => fn() => Async\graceful_shutdown(),
    ];

    foreach ($calls as $name => $call) {
        try {
            $call();
            echo "$name: no throw\n";
        } catch (Error $error) {
            echo "$name: ", $error->getMessage(), "\n";
        }
    }
});

echo "main: ", await($target), "\n";
echo "coroutines: ", count(Async\get_coroutines()), "\n";
?>
--EXPECT--
main: microtask A sched=1
spawn: The operation cannot be executed in the scheduler context
await: The operation cannot be executed in the scheduler context
suspend: The operation cannot be executed in the scheduler context
current_coroutine: The operation cannot be executed in the scheduler context
get_coroutines: The operation cannot be executed in the scheduler context
graceful_shutdown: The operation cannot be executed in the scheduler context
released A
target
coroutines: 1
