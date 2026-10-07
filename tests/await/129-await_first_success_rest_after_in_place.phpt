--TEST--
await_first_success() satisfied in place by a finished coroutine waits for the rest: a running coroutine's error joins the errors
--FILE--
<?php

use function Async\await_first_success;
use function Async\spawn;
use function Async\suspend;

$done = spawn(fn() => "first");
$running = spawn(function () {
    suspend();
    throw new RuntimeException("later");
});

suspend();
var_dump($done->isCompleted(), $running->isCompleted());

[$result, $errors] = await_first_success([$done, "r" => $running]);
var_dump($result);

foreach ($errors as $key => $error) {
    echo $key, ": ", $error->getMessage(), "\n";
}

?>
--EXPECT--
bool(true)
bool(false)
string(5) "first"
r: later
