--TEST--
S3.7 edge case: an exception the awaiting coroutine throws itself after await() returned is its own
--FILE--
<?php

use function Async\spawn;
use function Async\await;

$target = spawn(function() {
    return "fine";
});

// Thrown and caught in main after await() returned.
try {
    $value = await($target);
    echo "main got $value\n";
    throw new LogicException("main's own failure");
} catch (LogicException $e) {
    echo "main caught: ", $e->getMessage(), "\n";
}

// Thrown by a spawned waiter after await() returned; main gets it by awaiting the waiter.
$waiter = spawn(function() use ($target) {
    $value = await($target);
    echo "waiter got $value\n";
    throw new LogicException("waiter's own failure");
});

try {
    await($waiter);
    echo "no exception\n";
} catch (LogicException $e) {
    echo "main caught: ", $e->getMessage(), "\n";
    var_dump($e === $waiter->getException());
}

echo "end\n";
?>
--EXPECT--
main got fine
main caught: main's own failure
waiter got fine
main caught: waiter's own failure
bool(true)
end
