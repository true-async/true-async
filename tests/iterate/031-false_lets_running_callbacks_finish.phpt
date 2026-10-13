--TEST--
iterate(): false stops the walk and the running callbacks finish
--FILE--
<?php

use function Async\iterate;
use function Async\suspend;

iterate([1, 2, 3, 4, 5], function ($value) {
    echo "begin $value\n";
    if ($value === 3) {
        return false;
    }
    suspend();
    suspend();
    echo "end $value\n";
}, concurrency: 3);

echo "iterate returned\n";
?>
--EXPECT--
begin 1
begin 2
end 1
begin 3
end 2
iterate returned
