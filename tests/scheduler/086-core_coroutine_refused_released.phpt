--TEST--
A coroutine the core creates and cannot enqueue is released with the request, not leaked
--INI--
fiber.stack_size=1048576G
--FILE--
<?php
// No system maps a 1 PiB stack: the scheduler coroutine cannot be
// created, so the enqueue of the Fiber's and the GC's coroutines fails.
$fiber = new Fiber(function () {
    echo "not reached\n";
});

try {
    $fiber->start();
} catch (Exception $e) {
    echo $e->getMessage(), "\n";
}

var_dump($fiber->isStarted());

class Node {
    public $self;
}

$node = new Node();
$node->self = $node;
unset($node);

try {
    gc_collect_cycles();
} catch (Exception $e) {
    echo $e->getMessage(), "\n";
}

echo "end\n";
?>
--EXPECTF--
Fiber stack allocate failed: %s
bool(false)
Fiber stack allocate failed: %s
end
