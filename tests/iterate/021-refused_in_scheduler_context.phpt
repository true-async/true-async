--TEST--
iterate(): refused in scheduler context
--FILE--
<?php

use TrueAsync\Test;
use function Async\iterate;

Test\defer('A', null, function () {
    try {
        iterate([1], function ($value) {
            echo "callback $value\n";
        });
    } catch (Error $error) {
        echo get_class($error), ": ", $error->getMessage(), "\n";
    }
});

Async\suspend();
echo "end\n";
?>
--EXPECT--
microtask A sched=1
Error: The operation cannot be executed in the scheduler context
released A
end
