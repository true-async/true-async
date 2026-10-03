--TEST--
With async off (an output handler after the last drain) every Async\ function that needs a scheduler refuses; suspend() does nothing and protect() just calls its closure
--FILE--
<?php
use function Async\spawn;

$coroutine = spawn(fn() => 1);

ob_start(function (string $buffer) use ($coroutine) {
    $calls = [
        'spawn' => fn() => Async\spawn(fn() => 1),
        'await' => fn() => Async\await($coroutine),
        'current_coroutine' => fn() => Async\current_coroutine(),
        'get_coroutines' => fn() => Async\get_coroutines(),
        'graceful_shutdown' => fn() => Async\graceful_shutdown(),
    ];

    foreach ($calls as $name => $call) {
        try {
            $call();
            $buffer .= "$name: no throw\n";
        } catch (Error $error) {
            $buffer .= "$name: " . $error->getMessage() . "\n";
        }
    }

    $buffer .= "suspend: " . var_export(Async\suspend(), true) . "\n";
    $buffer .= "protect: " . Async\protect(fn() => "closure ran") . "\n";

    return $buffer;
});

echo "main end\n";
?>
--EXPECT--
main end
spawn: The operation cannot be executed while async is off
await: The operation cannot be executed while async is off
current_coroutine: The operation cannot be executed while async is off
get_coroutines: The operation cannot be executed while async is off
graceful_shutdown: The operation cannot be executed while async is off
suspend: NULL
protect: closure ran
