--TEST--
delay(0) is a yield: a queued coroutine runs first, and no Timer op is submitted
--FILE--
<?php
use function Async\{spawn, delay};
use TrueAsync\Test;

spawn(function () {
    echo "spawned runs\n";
});

echo "before\n";
delay(0);
echo "after\n";
var_dump(Test\reactor_state()['queue']);
?>
--EXPECT--
before
spawned runs
after
bool(false)
