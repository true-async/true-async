--TEST--
await_all() over a generator of Futures held by nothing else: the wait keeps each one alive until it completes
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\spawn;
use function Async\await_all;

$states = [];

function futures(array &$states)
{
    for ($i = 0; $i < 3; $i++) {
        $state = new FutureState();
        $states[] = $state;
        yield "f$i" => new Future($state);
    }
}

spawn(function () use (&$states) {
    while (count($states) < 3) {
        Async\suspend();
    }

    foreach ($states as $i => $state) {
        $state->complete($i * 10);
    }
});

[$results, $errors] = await_all(futures($states));
var_dump($results, $errors);

?>
--EXPECT--
array(3) {
  ["f0"]=>
  int(0)
  ["f1"]=>
  int(10)
  ["f2"]=>
  int(20)
}
array(0) {
}
