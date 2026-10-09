--TEST--
Future: a destructor run by the release of a child Future's mapper finds the child's WeakReference empty
--FILE--
<?php

use Async\Future;
use Async\FutureState;

final class Value
{
    public static WeakReference $child;

    public function __destruct()
    {
        echo "child reachable: ", var_export(self::$child->get() !== null, true), "\n";
    }
}

$state = new FutureState();
$value = new Value();
$child = (new Future($state))->map(function ($result) use ($value) {
    return $result;
});
$child->ignore();
unset($value);
Value::$child = WeakReference::create($child);
unset($child, $state);
echo "end\n";
?>
--EXPECT--
child reachable: false
end
