--TEST--
A garbage collection in a script that uses no coroutine installs no IO provider: the engine's GC coroutine is not a spawn
--FILE--
<?php
class Node
{
    public $self;

    public function __destruct()
    {
        echo "destructor\n";
    }
}

$node = new Node();
$node->self = $node;
unset($node);

var_dump(gc_collect_cycles() > 0);
var_dump(Io\Hooks\is_active());

Async\spawn(function () {});
var_dump(Io\Hooks\is_active());
?>
--EXPECT--
destructor
bool(true)
bool(false)
bool(true)
