--TEST--
await_first_success() keeps the error of a coroutine that finishes between the wake and the wait for the rest
--FILE--
<?php

use function Async\await_first_success;
use function Async\spawn;
use function Async\suspend;

$fast = spawn(function () {
    suspend();
    return "fast";
});

$late = spawn(function () {
    suspend();
    echo "late fails\n";
    throw new RuntimeException("late");
});

[$result, $errors] = await_first_success(['fast' => $fast, 'late' => $late]);

echo "result: $result\n";
foreach ($errors as $key => $error) {
    echo "$key: ", $error->getMessage(), "\n";
}

?>
--EXPECT--
late fails
result: fast
late: late
