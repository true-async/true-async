--TEST--
With no stack for the scheduler coroutine, suspend() in main is refused with the stack's exception and main runs on
--INI--
fiber.stack_size=64G
--FILE--
<?php
try {
    Async\suspend();
    echo "suspend returned\n";
} catch (Exception $e) {
    echo "suspend: ", $e->getMessage(), "\n";
}

var_dump(Async\current_coroutine()->isRunning());
echo "end\n";
?>
--EXPECTF--
suspend: Fiber stack allocate failed: %s
bool(true)
end
