--TEST--
A finally handler that suspends lets the next one run, and the waiter resumes before it ends
--FILE--
<?php

use function Async\await;
use function Async\delay;
use function Async\spawn;

$first_ended = false;
$coroutine = spawn(function () {
    $self = Async\current_coroutine();
    $self->finally(function () {
        echo "first starts\n";
        delay(10);
        echo "first ends\n";
        $GLOBALS['first_ended'] = true;
    });
    $self->finally(function () {
        echo "second\n";
    });
    echo "body\n";
});

await($coroutine);
echo "awaited\n";

// A longer delay could end first: a timer due when main parks wakes it in place, ahead of the queue
while (!$first_ended) {
    delay(1);
}

echo "end\n";

?>
--EXPECT--
body
first starts
second
awaited
first ends
end
