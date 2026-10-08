--TEST--
A coroutine that cannot get a stack while the GC root buffer is full ends the request instead of starting GC coroutines forever
--FILE--
<?php
class Node
{
    public $self;

    public function __destruct()
    {
    }
}

register_shutdown_function(function () {
    echo "shutdown\n";
});

$nodes = [];

for ($i = 0; $i < 10001; $i++) {
    $node = new Node();
    $node->self = $node;
    $nodes[] = $node;
}

$coroutine = Async\spawn(function () use ($nodes) {
    echo "ran\n";
});
// After the spawn, which created the scheduler with its own stack; 1 PiB is more than mmap
// or VirtualAlloc places without an address hint, so every system refuses it.
ini_set('fiber.stack_size', '1048576G');
unset($nodes, $node);
echo "end\n";
?>
--EXPECTF--
Fatal error: Uncaught Exception: Fiber stack allocate failed: %s in %s:%d
Stack trace:
#0 {main}
  thrown in %s on line %d
shutdown
