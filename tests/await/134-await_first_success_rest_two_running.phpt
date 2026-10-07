--TEST--
await_first_success() satisfied in place waits for every running coroutine of the rest, and keeps each one's error
--FILE--
<?php

use function Async\await_first_success;
use function Async\spawn;
use function Async\suspend;

$done = spawn(fn() => "first");
$a = spawn(function () {
    suspend();
    throw new RuntimeException("a");
});
$b = spawn(function () {
    suspend();
    suspend();
    throw new RuntimeException("b");
});

suspend();
[$result, $errors] = await_first_success([$done, "a" => $a, "b" => $b]);
var_dump($result, $a->isCompleted(), $b->isCompleted());
ksort($errors);

foreach ($errors as $key => $error) {
    echo $key, ": ", $error->getMessage(), "\n";
}

?>
--EXPECT--
string(5) "first"
bool(true)
bool(true)
a: a
b: b
