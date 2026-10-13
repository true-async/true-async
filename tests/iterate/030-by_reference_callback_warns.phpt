--TEST--
iterate(): a callback taking its value by reference gets the by-reference warning
--FILE--
<?php

use function Async\iterate;

$data = [1, 2];

iterate($data, function (&$value) {
    $value *= 10;
    echo "value $value\n";
});

var_dump($data);
?>
--EXPECTF--

Warning: {closure:%s:7}(): Argument #1 ($value) must be passed by reference, value given in Unknown on line 0
value 10

Warning: {closure:%s:7}(): Argument #1 ($value) must be passed by reference, value given in Unknown on line 0
value 20
array(2) {
  [0]=>
  int(1)
  [1]=>
  int(2)
}
