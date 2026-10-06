--TEST--
await_all() and await_all_or_fail() skip null triggers: they are not waited for and leave no result
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\spawn;
use function Async\await_all;
use function Async\await_all_or_fail;

$first = new FutureState();
$second = new FutureState();

spawn(function () use ($first, $second) {
    $first->complete(1);
    $second->complete(2);
});

[$results, $errors] = await_all([new Future($first), null, 'key' => new Future($second)]);
var_dump($results, $errors);

var_dump(await_all_or_fail([null, null]));

?>
--EXPECT--
array(2) {
  [0]=>
  int(1)
  ["key"]=>
  int(2)
}
array(0) {
}
array(0) {
}
