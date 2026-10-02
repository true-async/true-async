--TEST--
A coroutine cannot await itself, in main or in a spawned coroutine
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\current_coroutine;

try {
    await(current_coroutine());
} catch (Error $error) {
    echo "main: ", $error->getMessage(), "\n";
}

$coroutine = spawn(function () {
    try {
        await(current_coroutine());
    } catch (Error $error) {
        echo "coroutine: ", $error->getMessage(), "\n";
    }

    return "done";
});

echo await($coroutine) . "\n";
?>
--EXPECT--
main: Cannot await a coroutine from within itself
coroutine: Cannot await a coroutine from within itself
done
