--TEST--
With no stack for the scheduler coroutine, a microtask, a spawn and a wake of main are refused with the stack's exception
--INI--
fiber.stack_size=64G
--FILE--
<?php
use function Async\spawn;

try {
    TrueAsync\Test\defer('a');
} catch (Exception $e) {
    echo "defer: ", $e->getMessage(), "\n";
}

try {
    spawn(function () {
        echo "body\n";
    });
} catch (Exception $e) {
    echo "spawn: ", $e->getMessage(), "\n";
}

// The running main woken with an error: the stack check comes first, and the error is released.
try {
    TrueAsync\Test\enqueue_with_error(Async\current_coroutine(), new Exception("woken"), true);
} catch (Exception $e) {
    echo "wake: ", $e->getMessage(), "\n";
}

echo "end\n";
?>
--EXPECTF--
released a
defer: Fiber stack allocate failed: mmap failed: %s
spawn: Fiber stack allocate failed: mmap failed: %s
wake: Fiber stack allocate failed: mmap failed: %s
end
