--TEST--
await_any_of(2) collecting errors counts successes, not completions: a failure and one success do not satisfy it
--FILE--
<?php

use function Async\await_any_of;
use function Async\spawn;
use function Async\suspend;

$fail = spawn(function () {
    suspend();
    throw new RuntimeException("failed");
});
$first = spawn(function () {
    suspend();
    suspend();
    return "first";
});
$second = spawn(function () {
    suspend();
    suspend();
    suspend();
    return "second";
});

[$results, $errors] = await_any_of(2, ["fail" => $fail, "first" => $first, "second" => $second]);
var_dump($results);

foreach ($errors as $key => $error) {
    echo $key, ": ", $error->getMessage(), "\n";
}

?>
--EXPECT--
array(2) {
  ["first"]=>
  string(5) "first"
  ["second"]=>
  string(6) "second"
}
fail: failed
