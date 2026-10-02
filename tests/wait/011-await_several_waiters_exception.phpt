--TEST--
S3.7 items 7 and 3: every waiter of a coroutine that ends by an exception gets that same exception object
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\suspend;

$target = spawn(function() {
    suspend();
    throw new RuntimeException("shared failure");
});

$probe = function() use ($target) {
    try {
        await($target);
        return "no exception";
    } catch (RuntimeException $e) {
        return $e === $target->getException() ? "same exception" : "another exception";
    }
};

$w1 = spawn($probe);
$w2 = spawn($probe);

// Queue [target, w1, w2]; target's suspend() lets w1 and w2 park on it before it throws.
try {
    await($target);
    echo "main: no exception\n";
} catch (RuntimeException $e) {
    echo "main: ", $e === $target->getException() ? "same exception" : "another exception", "\n";
    echo "message: ", $e->getMessage(), "\n";
}

echo "w1: ", await($w1), "\n";
echo "w2: ", await($w2), "\n";
echo "end\n";
?>
--EXPECT--
main: same exception
message: shared failure
w1: same exception
w2: same exception
end
