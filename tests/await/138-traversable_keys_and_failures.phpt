--TEST--
await_all() over a Traversable: null keys take the next index and a null item is skipped
--FILE--
<?php
use Async\Future;
use function Async\await_all;

var_dump(await_all((function () {
    yield null => Future::completed('a');
    yield null => Future::completed('b');
    yield null;
    yield 'k' => Future::completed('c');
})())[0]);
?>
--EXPECT--
array(3) {
  [0]=>
  string(1) "a"
  [1]=>
  string(1) "b"
  ["k"]=>
  string(1) "c"
}
