--TEST--
Future: the result's destructor run by the FutureState's free finds the state's WeakReference empty
--FILE--
<?php

use Async\Future;
use Async\FutureState;

final class Value
{
    public static WeakReference $state;

    public function __destruct()
    {
        echo "state reachable: ", var_export(self::$state->get() !== null, true), "\n";
    }
}

$state = new FutureState();
$state->complete(new Value());
(new Future($state))->ignore();
Value::$state = WeakReference::create($state);
unset($state);
echo "end\n";
?>
--EXPECT--
state reachable: false
end
