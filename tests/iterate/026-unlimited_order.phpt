--TEST--
iterate(): with no limit a worker is added for the next element while a callback waits
--FILE--
<?php

use function Async\iterate;
use function Async\suspend;

iterate([1, 2, 3], function ($value) {
    echo "begin $value\n";
    suspend();
    echo "end $value\n";
});
?>
--EXPECT--
begin 1
end 1
begin 2
begin 3
end 2
end 3
