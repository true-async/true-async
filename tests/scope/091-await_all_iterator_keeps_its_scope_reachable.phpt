--TEST--
get_deadlocked_coroutines(): while an await_all() iterator can still throw, the members of its scope's subtree and their awaitCompletion() waiters are not found
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\spawn;
use function Async\delay;
use function Async\await_all;
use function Async\get_deadlocked_coroutines;

$child = null;
function items(&$child): Generator
{
    $child = Scope::inherit();
    $child->spawn(function () {
        try {
            await(new Future(new FutureState()));
        } catch (Throwable $e) {
            echo "member: ", $e->getMessage(), "\n";
        }
    });
    delay(50);
    throw new RuntimeException("items failed");
    yield 1;
}

$callerScope = Scope::inherit()->asNotSafely();
$callerScope->spawn(function () use (&$child) {
    try {
        await_all(items($child));
    } catch (Throwable $e) {
        echo "caller: ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});
delay(10);
spawn(function () use ($child) {
    try {
        $child->awaitCompletion(new Future(new FutureState()));
        echo "waiter: completed\n";
    } catch (Throwable $e) {
        echo "waiter: ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});
$child = null;
delay(10);
echo "found: ", count(get_deadlocked_coroutines()), "\n";
delay(100);
echo "end\n";
?>
--EXPECT--
found: 0
member: Cancellation of the iterator due to an exception
waiter: Async\AsyncCancellation: Cancellation of the iterator due to an exception
caller: RuntimeException: items failed
end
