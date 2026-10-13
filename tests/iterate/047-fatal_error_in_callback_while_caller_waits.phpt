--TEST--
iterate(): a fatal error in a callback while the caller waits ends the request
--FILE--
<?php

use function Async\await;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

register_shutdown_function(function () {
    echo "shutdown\n";
});

await(spawn(function () {
    try {
        iterate([1, 2], function ($value) {
            suspend();
            if ($value === 2) {
                eval('function twice() {} function twice() {}');
            }
            echo "callback $value finished\n";
        });
    } finally {
        echo "caller finally\n";
    }
}));
?>
--EXPECTF--
callback 1 finished

Fatal error: Cannot redeclare function twice() (previously declared in %s(17) : eval()'d code:1) in %s(17) : eval()'d code on line 1
shutdown
