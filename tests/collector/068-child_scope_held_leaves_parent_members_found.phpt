--TEST--
get_deadlocked_coroutines(): a Scope object main holds keeps the members of its scope, while the parent's own member is found when the parent's object is held only by a stuck coroutine
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start_parent(): Scope
{
    $parent = new Scope();
    $parent->spawn(function () {
        await(new Future(new FutureState()));
    });
    $child = Scope::inherit($parent);
    $child->spawn(function () {
        try {
            await(new Future(new FutureState()));
        } catch (Async\AsyncCancellation $e) {
            echo "child member: ", $e->getMessage(), "\n";
        }
    });
    spawn(function () use ($parent) {
        await(new Future(new FutureState()));
    });
    return $child;
}

$child = start_parent();
// A yield, not a delay: a timer already due when main parks resumes main before the queued members run.
suspend();
$found = get_deadlocked_coroutines();
echo "found: ", count($found), "\n";
$child->cancel();
suspend();
foreach ($found as $coroutine) {
    $coroutine->cancel();
}
suspend();
echo "end\n";
?>
--EXPECT--
found: 2
child member: Scope was cancelled
end
