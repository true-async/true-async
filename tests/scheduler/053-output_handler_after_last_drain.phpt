--TEST--
Output handlers run after the last drain, with async off: a final handler cannot spawn
--FILE--
<?php
use function Async\spawn;
use function Async\await;

ob_start(function (string $buffer, int $phase) {
    if (!($phase & PHP_OUTPUT_HANDLER_FINAL)) {
        return $buffer;
    }

    try {
        spawn(fn() => 1);
        $message = "spawned";
    } catch (Error $error) {
        $message = $error->getMessage();
    }

    return $buffer . "final handler: " . $message . "\n";
});

spawn(function () {
    echo "coroutine output\n";
});

register_shutdown_function(function () {
    await(spawn(fn() => print("shutdown coroutine output\n")));
});

echo "main end\n";
?>
--EXPECT--
main end
coroutine output
shutdown coroutine output
final handler: The operation cannot be executed while async is off
