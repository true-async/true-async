--TEST--
await_all() holds the token's event when its Future is constructed again during the wait
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await_all;
use function Async\spawn;
use function Async\suspend;

$token = new Future(new FutureState());

$worker = spawn(function () use ($token) {
    suspend();
    $other = new FutureState();
    $other->ignore();
    $token->__construct($other);
    echo "token reconstructed\n";
    suspend();
    return "done";
});

[$results, $errors] = await_all([$worker], $token);

var_dump($results, count($errors));

?>
--EXPECT--
token reconstructed
array(1) {
  [0]=>
  string(4) "done"
}
int(0)
