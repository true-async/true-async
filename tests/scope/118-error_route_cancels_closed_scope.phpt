--TEST--
Scope: an error route that reaches a closed scope which was not cancelled marks it cancelled, as TrueAsync's catch_or_cancel; awaitAfterCancellation() then waits for its subtree instead of returning at once
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;

$gate = new FutureState();
$thrown = new FutureState();
$checked = new FutureState();
$outer = (new Scope())->allowZombies();
$inner = Scope::inherit($outer);
$failing = $inner->spawn(function () use ($gate, $thrown) {
    (new Future($gate))->await();
    $thrown->complete(1);
    throw new RuntimeException("first zombie failed");
});
$second = $inner->spawn(function () use ($checked) {
    (new Future($checked))->await();
    echo "second zombie: done\n";
});
while ($failing->getAwaitingInfo() === [] || $second->getAwaitingInfo() === []) {
    Async\suspend();
}
$inner->cancel();
$outer->cancel();
echo "outer: closed ", var_export($outer->isClosed(), true), ", cancelled ", var_export($outer->isCancelled(), true), "\n";
$outer->awaitAfterCancellation();
echo "before the error: returned at once\n";
$gate->complete(1);
(new Future($thrown))->await();
echo "outer: cancelled ", var_export($outer->isCancelled(), true), "\n";
$checked->complete(1);
$outer->awaitAfterCancellation();
echo "after the error: returned once the subtree was empty\n";
echo "failing: ", $failing->getException()->getMessage(), "\n";
?>
--EXPECT--
outer: closed true, cancelled false
before the error: returned at once
outer: cancelled true
second zombie: done
after the error: returned once the subtree was empty
failing: first zombie failed
