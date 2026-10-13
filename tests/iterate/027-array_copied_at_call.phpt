--TEST--
iterate(): the array is copied at the call, a change of the caller's array is not walked
--FILE--
<?php

use function Async\iterate;
use function Async\suspend;

$data = [1, 2, 3];

iterate($data, function ($value) use (&$data) {
    $data[] = $value * 10;
    suspend();
    echo "value $value\n";
});

echo "count: ", count($data), "\n";
?>
--EXPECT--
value 1
value 2
value 3
count: 6
