--TEST--
delay(): with async off (an output handler after the last drain) there is no coroutine to park, and it returns at once
--FILE--
<?php
ob_start(function (string $buffer) {
    // With no coroutine current, delay() returns before it arms a Timer.
    $coroutines = TrueAsync\Test\coroutine_count();
    Async\delay(1000);

    return $buffer . "coroutines: $coroutines\ndelay: returned\n";
});

echo "main end\n";
?>
--EXPECT--
main end
coroutines: 0
delay: returned
