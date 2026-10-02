--TEST--
A script with 12 000 cyclic objects with __destruct ends, the collections run in main and in a coroutine
--FILE--
<?php
use function Async\spawn;
use function Async\await;

class Node
{
    public static int $destructed = 0;
    public $self;

    public function __destruct()
    {
        self::$destructed++;
    }
}

function make_cycles(int $count): void
{
    for ($i = 0; $i < $count; $i++) {
        $node = new Node();
        $node->self = $node;
    }
}

make_cycles(12000);
gc_collect_cycles();
echo "main: ", Node::$destructed, "\n";

await(spawn(function () {
    make_cycles(12000);
    gc_collect_cycles();
}));

echo "coroutine: ", Node::$destructed, "\n";
?>
--EXPECT--
main: 12000
coroutine: 24000
