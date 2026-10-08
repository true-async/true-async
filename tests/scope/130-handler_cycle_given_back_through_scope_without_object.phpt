--TEST--
Scope: a member leaving a child scope whose object is gone gives the parent's object back to the GC through it, and the parent's handler cycle is collected after the last member
--FILE--
<?php

use Async\Scope;
use function Async\suspend;

$is_released = false;
$parent = new Scope();
$parent->setExceptionHandler(function (Scope $s, Async\Coroutine $c, Throwable $e) use ($parent) {});
$child = Scope::inherit($parent);
$members = [];

for ($i = 0; $i < 2; $i++) {
    $members[] = $child->spawn(function () use ($i) {
        while (!$GLOBALS['is_released']) {
            try {
                suspend();
            } catch (Async\AsyncCancellation $e) {
            }
        }
        if ($i === 1) {
            suspend();
        }
    });
}
suspend();

$weak_parent = WeakReference::create($parent);
// The child scope object's destruction cancels the child scope, which the members outlive.
unset($parent, $child);
gc_collect_cycles();
echo "alive while members run: ", var_export($weak_parent->get() !== null, true), "\n";

$is_released = true;
while (!$members[1]->isCompleted()) {
    suspend();
}
unset($members);
gc_collect_cycles();
suspend();
gc_collect_cycles();
echo "alive after: ", var_export($weak_parent->get() !== null, true), "\n";

?>
--EXPECT--
alive while members run: true
alive after: false
