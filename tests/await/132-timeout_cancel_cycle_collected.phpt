--TEST--
A Timeout cancelled with a cancellation that holds the Timeout is a cycle the GC collects
--FILE--
<?php

use function Async\timeout;

class Stop extends Async\AsyncCancellation
{
    public $timeout;
}

$timeout = timeout(60000);
$stop = new Stop("stop");
$stop->timeout = $timeout;
$timeout->cancel($stop);
unset($timeout, $stop);

var_dump(gc_collect_cycles() >= 2);

?>
--EXPECT--
bool(true)
