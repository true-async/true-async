--TEST--
await_any_of() stops its pass at the completed trigger that satisfies it: no places after it
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await_any_of;

$done = new FutureState();
$done->complete("first");
$pending = new FutureState();
$pending->ignore();

[$results, $errors] = await_any_of(1, ['a' => new Future($done), 'b' => new Future($pending)], fillNull: true);

var_dump($results, $errors);

?>
--EXPECT--
array(1) {
  ["a"]=>
  string(5) "first"
}
array(0) {
}
