--TEST--
await_first_success() returns after the coroutines still running finish, and their errors join the errors
--FILE--
<?php

use function Async\spawn;
use function Async\await_first_success;
use function Async\suspend;

$fast = spawn(function () {
    return "fast";
});

$slow = spawn(function () {
    suspend();
    suspend();
    echo "slow fails\n";
    throw new RuntimeException("slow");
});

[$result, $errors] = await_first_success(['fast' => $fast, 'slow' => $slow]);

echo "result: $result\n";
foreach ($errors as $key => $error) {
    echo "$key: ", $error->getMessage(), "\n";
}

?>
--EXPECT--
slow fails
result: fast
slow: slow
