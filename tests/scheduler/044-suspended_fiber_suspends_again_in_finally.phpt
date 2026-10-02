--TEST--
A fiber left suspended is closed with a graceful exit and no deadlock; it may suspend again in finally (D5), and the next graceful exit unwinds it
--INI--
true_async.debug_deadlock=1
--FILE--
<?php
$fiber = new Fiber(function () {
    try {
        echo "fiber suspends\n";
        Fiber::suspend();
        echo "not reached\n";
    } finally {
        echo "finally suspends\n";

        try {
            Fiber::suspend();
            echo "not reached either\n";
        } finally {
            echo "finally ends\n";
        }
    }
});

$fiber->start();
echo "main end\n";
?>
--EXPECT--
fiber suspends
main end
finally suspends
finally ends
