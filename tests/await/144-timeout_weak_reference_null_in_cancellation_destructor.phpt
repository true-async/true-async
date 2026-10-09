--TEST--
Async\timeout(): the cancellation's destructor run by the Timeout's free finds the Timeout's WeakReference empty
--FILE--
<?php

final class Cancellation extends Async\AsyncCancellation
{
    public static WeakReference $timeout;

    public function __destruct()
    {
        echo "timeout reachable: ", var_export(self::$timeout->get() !== null, true), "\n";
    }
}

$timeout = Async\timeout(1000);
$timeout->cancel(new Cancellation());
Cancellation::$timeout = WeakReference::create($timeout);
unset($timeout);
echo "end\n";
?>
--EXPECT--
timeout reachable: false
end
