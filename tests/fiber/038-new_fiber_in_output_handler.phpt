--TEST--
A fiber started in an output handler, after the deactivation, runs as a plain fiber: start, suspend and resume work
--FILE--
<?php
ob_start(function ($buffer) {
    $fiber = new Fiber(function () {
        $value = Fiber::suspend('suspended');

        return "returned $value";
    });

    $out = "start: " . $fiber->start() . "\n";
    $fiber->resume('resumed');

    return $buffer . $out . "end: " . $fiber->getReturn() . "\n";
});

Async\spawn(function () {
    echo "coroutine\n";
});

echo "main end\n";
?>
--EXPECT--
main end
coroutine
start: suspended
end: returned resumed
