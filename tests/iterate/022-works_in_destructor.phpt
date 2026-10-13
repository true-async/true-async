--TEST--
iterate(): works in a destructor
--FILE--
<?php

use function Async\await;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

class Walker
{
    public function __destruct()
    {
        iterate([1, 2], function ($value) {
            suspend();
            echo "destructor value $value\n";
        });
        echo "destructor done\n";
    }
}

await(spawn(function () {
    $walker = new Walker();
    unset($walker);
    echo "after unset\n";
}));
?>
--EXPECT--
destructor value 1
destructor value 2
destructor done
after unset
