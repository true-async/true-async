--TEST--
exit() in a finally handler ends the request at once
--DESCRIPTION--
TrueAsync's finally run takes the exit as the worker's and the script goes on until it ends.
--FILE--
<?php

use function Async\await;
use function Async\delay;
use function Async\spawn;

register_shutdown_function(function () {
    echo "shutdown\n";
});

$coroutine = spawn(function () {
    $self = Async\current_coroutine();
    $self->finally(function () {
        echo "exits\n";
        exit(3);
    });
    $self->finally(function () {
        echo "not reached\n";
    });
    echo "body\n";
});

await($coroutine);
echo "not reached\n";
delay(30);

?>
--EXPECT--
body
exits
shutdown
