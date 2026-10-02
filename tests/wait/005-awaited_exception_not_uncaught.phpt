--TEST--
S3.7 item 4: an exception delivered by await() does not also end the request as uncaught
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\suspend;

// Caught before the coroutine finished; no variable holds the coroutine.
try {
    await(spawn(function() {
        suspend();
        throw new RuntimeException("first");
    }));
} catch (RuntimeException $e) {
    echo "caught: ", $e->getMessage(), "\n";
}

// Caught after the coroutine finished (it ran during suspend()), then the variable is dropped.
$coroutine = spawn(function() {
    throw new RuntimeException("second");
});
suspend();
try {
    await($coroutine);
} catch (RuntimeException $e) {
    echo "caught: ", $e->getMessage(), "\n";
}
unset($coroutine, $e);

// Caught by a spawned waiter; neither coroutine is held by a variable.
spawn(function() {
    try {
        await(spawn(function() {
            throw new RuntimeException("third");
        }));
    } catch (RuntimeException $e) {
        echo "spawned waiter caught: ", $e->getMessage(), "\n";
    }
});

echo "end of main\n";
?>
--EXPECT--
caught: first
caught: second
end of main
spawned waiter caught: third
