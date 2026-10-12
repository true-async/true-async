--TEST--
await_any_of(): a Traversable that gives a key twice releases the replaced result after storing the new one, so a destructor that suspends while the wait ends writes into no freed table and wakes no one
--FILE--
<?php

use Async\Future;
use Async\FutureState;

class SuspendingDestructor
{
    public function __construct(private Future $late)
    {
    }

    public function __destruct()
    {
        echo "destructor suspends\n";
        $this->late->await();
        echo "not reached\n";
    }
}

// The second result under 'k' frees the first, whose destructor suspends the walk. Meanwhile the pending item
// completes the wait, whose end frees the results table and cancels the walk; a store into that table after the
// release would write into freed memory.
$late_state = new FutureState();
$late = new Future($late_state);
$state = new FutureState();
$pending = new Future($state);

function items(Future $pending, Future $late): Generator
{
    yield 'k' => Future::completed(new SuspendingDestructor($late));
    yield 'p' => $pending;
    // Under another order (a random scheduling seed) the walk may end before it.
    yield 'k' => Future::completed(2)->ignore();
}

// Behind the walk, which runs to the destructor's park in one go.
Async\spawn(function () use ($state) {
    Async\suspend();
    Async\suspend();
    $state->complete(1);
});

[$results, $errors] = Async\await_any_of(2, items($pending, $late));
echo implode(", ", array_keys($results)), "\n";

Async\suspend();
Async\suspend();
echo "end\n";
// Completed after the walk was cancelled. Under another order (a random scheduling seed) the destructor runs
// later, and this ends its wait.
$late_state->complete(null);
?>
--EXPECT--
destructor suspends
k, p
end
