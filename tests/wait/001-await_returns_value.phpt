--TEST--
S3.7 item 1: await() returns the value the coroutine's callable returned
--FILE--
<?php

use function Async\spawn;
use function Async\await;

$object = new stdClass();
$object->name = "payload";

$int = spawn(function() {
    return 42;
});
$string = spawn(function() {
    return "result";
});
$array = spawn(function() {
    return [1, "two" => 2];
});
$obj = spawn(function() use ($object) {
    return $object;
});
// spawn() passes its extra arguments to the callable: 20 + 22 = 42
$args = spawn(function(int $a, int $b) {
    return $a + $b;
}, 20, 22);

var_dump(await($int));
var_dump(await($string));
var_dump(await($array));
var_dump(await($obj) === $object);
var_dump(await($args));

echo "end\n";
?>
--EXPECT--
int(42)
string(6) "result"
array(2) {
  [0]=>
  int(1)
  ["two"]=>
  int(2)
}
bool(true)
int(42)
end
