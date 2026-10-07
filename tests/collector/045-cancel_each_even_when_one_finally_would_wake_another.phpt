--TEST--
The automatic run with cancel, set at run time: every stuck coroutine is cancelled in the same run, even one whose target another stuck coroutine completes in its finally
--INI--
true_async.partial_deadlock_interval=0
error_reporting=E_ALL & ~E_WARNING
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\delay;

function start(): void
{
    $state = new FutureState();
    $future = new Future($state);

    spawn(function () use ($state) {
        try {
            (new Future(new FutureState()))->await();
        } catch (Async\AsyncCancellation $e) {
            echo "a: ", $e->getMessage(), "\n";
        } finally {
            $state->complete("value");
        }
    });
    spawn(function () use ($future) {
        try {
            $value = $future->await();
            echo "b: ", $value, "\n";
        } catch (Async\AsyncCancellation $e) {
            echo "b: ", $e->getMessage(), "\n";
        }
    });
}

var_dump(ini_set('true_async.partial_deadlock', 'cancel'));
start();
delay(10);
delay(10);
echo "end\n";
?>
--EXPECT--
string(6) "report"
a: Deadlock detected
b: Deadlock detected
end
