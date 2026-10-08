--TEST--
A waiter in awaitCompletion() stays live while any coroutine of the scope's subtree can finish, the scope's second member or a member of its second child scope included; the scope it holds then keeps the stuck members live too
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\current_coroutine;
use function Async\get_coroutines;
use function Async\get_deadlocked_coroutines;

// Only the waiters hold the scopes; main keeps the coroutines' ids, which hold nothing.
$gate = new FutureState();
$members = new Scope();
$names = [
    $members->spawn(fn() => (new Future(new FutureState()))->await())->getId() => 'stuck member',
    $members->spawn(fn() => (new Future($gate))->await())->getId() => 'live member',
];

$children = new Scope();
$first_child = Scope::inherit($children);
$second_child = Scope::inherit($children);
$names += [
    $first_child->spawn(fn() => (new Future(new FutureState()))->await())->getId() => 'stuck member of the first child scope',
    $second_child->spawn(fn() => (new Future($gate))->await())->getId() => 'live member of the second child scope',
];

$names[spawn(function () use ($members) {
    $members->awaitCompletion(new Future(new FutureState()));
})->getId()] = 'waiter on the members';
$names[spawn(function () use ($children, $first_child, $second_child) {
    $children->awaitCompletion(new Future(new FutureState()));
})->getId()] = 'waiter on the child scopes';
unset($members, $children, $first_child, $second_child);

$parked = fn() => array_filter(get_coroutines(), fn($coroutine) => $coroutine !== current_coroutine() &&
    $coroutine->getAwaitingInfo() === []) === [];

while (!$parked()) {
    suspend();
}

$found = array_map(fn($coroutine) => $coroutine->getId(), get_deadlocked_coroutines());

foreach ($names as $id => $name) {
    echo $name, ": ", in_array($id, $found, true) ? "found" : "live", "\n";
}

foreach (get_coroutines() as $coroutine) {
    if (str_starts_with($names[$coroutine->getId()] ?? '', 'stuck')) {
        $coroutine->cancel();
    }
}

$gate->complete(1);
?>
--EXPECT--
stuck member: live
live member: live
stuck member of the first child scope: live
live member of the second child scope: live
waiter on the members: live
waiter on the child scopes: live
