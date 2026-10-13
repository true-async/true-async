--TEST--
iterate(): exit() in a callback ends the request, the caller's finally runs
--FILE--
<?php

use function Async\await;
use function Async\iterate;
use function Async\spawn;

register_shutdown_function(function () {
    echo "shutdown\n";
});

await(spawn(function () {
    try {
        iterate([1, 2, 3], function ($value) {
            echo "value $value\n";
            if ($value === 2) {
                exit(0);
            }
        });
    } finally {
        echo "caller finally\n";
    }
    echo "after iterate\n";
}));

echo "main after\n";
?>
--EXPECT--
value 1
value 2
caller finally
shutdown
