--TEST--
get_deadlocked_coroutines(): members of a child scope are not found while main holds the parent's Scope object, even when the child's own object is held only by a stuck coroutine
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

function start_child(Scope $parent): void
{
    $child = Scope::inherit($parent);
    $child->spawn(function () {
        try {
            await(new Future(new FutureState()));
        } catch (Async\AsyncCancellation $e) {
            echo "member: ", $e->getMessage(), "\n";
        }
    });
    spawn(function () use ($child) {
        await(new Future(new FutureState()));
    });
}

$parent = new Scope();
start_child($parent);
delay(10);
$found = get_deadlocked_coroutines();
echo "found: ", count($found), "\n";
$parent->cancel();
delay(10);
foreach ($found as $coroutine) {
    $coroutine->cancel();
}
delay(10);
echo "end\n";
?>
--EXPECT--
found: 1
member: Scope was cancelled
end
