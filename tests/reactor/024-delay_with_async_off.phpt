--TEST--
delay(): with async off (an output handler after the last drain) there is no coroutine to park, and it returns at once
--FILE--
<?php
ob_start(function (string $buffer) {
    $before = hrtime(true);
    Async\delay(5000);
    $elapsed = (hrtime(true) - $before) / 1e6;

    return $buffer . "delay: " . ($elapsed < 1000 ? "returned at once" : "waited $elapsed ms") . "\n";
});

echo "main end\n";
?>
--EXPECT--
main end
delay: returned at once
