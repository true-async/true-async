--TEST--
await_all() with one pending coroutine twice among the triggers and as the token: a slot per record, and the token wins
--FILE--
<?php

use Async\OperationCanceledException;
use function Async\spawn;
use function Async\await_all;
use function Async\suspend;

$coroutine = spawn(function () {
    suspend();
    return "value";
});

try {
    await_all([$coroutine, 'again' => $coroutine], $coroutine);
    echo "not reached\n";
} catch (OperationCanceledException $e) {
    echo $e->getMessage(), "\n";
}

$other = spawn(function () {
    suspend();
    return "other";
});

[$results, $errors] = await_all([$other, 'again' => $other]);
var_dump($results);

?>
--EXPECT--
Operation has been cancelled
array(2) {
  [0]=>
  string(5) "other"
  ["again"]=>
  string(5) "other"
}
