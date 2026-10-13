--TEST--
Scope: isFinished() counts every child scope, not only the first: a busy second child scope
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\suspend;

$gate = new FutureState();
$parent = new Scope();
$idle = Scope::inherit($parent);
$busy = Scope::inherit($parent);
$member = $busy->spawn(fn() => (new Future($gate))->await());
while ($member->getAwaitingInfo() === []) {
    suspend();
}
echo "busy second child scope: finished ", var_export($parent->isFinished(), true), "\n";
$gate->complete(1);
while (!$member->isCompleted()) {
    suspend();
}
echo "after its member: finished ", var_export($parent->isFinished(), true), "\n";
?>
--EXPECT--
busy second child scope: finished false
after its member: finished true
