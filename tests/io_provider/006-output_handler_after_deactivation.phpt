--TEST--
IO in an output handler at the request's end, after async is deactivated, runs synchronously
--FILE--
<?php
ob_start(function ($buffer) {
    usleep(1000);

    return $buffer . "handler slept\n";
});

Async\spawn(function () { echo "spawned\n"; });
echo "main\n";
?>
--EXPECT--
main
spawned
handler slept
