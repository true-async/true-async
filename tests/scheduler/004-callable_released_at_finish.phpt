--TEST--
A finished coroutine releases its callable at once, not when its object dies
--FILE--
<?php
class Held
{
    public function __destruct()
    {
        echo "held released\n";
    }
}

$held = new Held;
$first = Async\spawn(function () use ($held) {
    echo "first\n";
});
unset($held);

Async\spawn(function () {
    echo "second\n";
});

echo "end\n";
?>
--EXPECT--
end
first
held released
second
