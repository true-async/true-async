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

ini_set('fiber.stack_size', '1');
$coroutine = Async\spawn(function () use ($nodes) {
    echo "ran\n";
});
unset($nodes, $node);
echo "end\n";
?>
--EXPECTF--
end

Fatal error: Uncaught Exception: Fiber stack size is too small, it needs to be at least %d bytes in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
shutdown
