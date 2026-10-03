--TEST--
A finished coroutine keeps its arguments, positional and named: a cycle through them is collected
--FILE--
<?php
use function Async\spawn;
use function Async\await;

class Box
{
    public $coroutine;

    public function __construct(private string $name) {}

    public function __destruct()
    {
        echo "released ", $this->name, "\n";
    }
}

$positional = new Box("positional");
// The second argument: a cycle through the first only would not show that each one is reported.
$positional->coroutine = spawn(fn($first, $box) => 1, "first", $positional);
await($positional->coroutine);

$named = new Box("named");
$named->coroutine = spawn(fn(...$arguments) => 2, box: $named);
await($named->coroutine);

unset($positional, $named);
echo "collecting\n";
gc_collect_cycles();
echo "main end\n";
?>
--EXPECT--
collecting
released positional
released named
main end
