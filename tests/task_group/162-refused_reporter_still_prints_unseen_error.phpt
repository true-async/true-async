--TEST--
TaskGroup: errors nobody saw whose reporter cannot get a coroutine stack are still printed at the request's end; the release throws the refusal
--FILE--
<?php
$group = new Async\TaskGroup();
$group->spawn(function () {
    throw new Exception("task failed");
});
$group->close();
$group->awaitCompletion();

// The script's end ends the scheduler: the reporter needs a new one, which the shutdown function cannot have.
register_shutdown_function(function () {
    ini_set('fiber.stack_size', '1048576G'); // 1 PiB: beyond what mmap and VirtualAlloc can place, refused everywhere

    try {
        unset($GLOBALS['group']);
        echo "not reached\n";
    } catch (Exception $exception) {
        echo "release: ", $exception->getMessage(), "\n";
    }

    ini_restore('fiber.stack_size');
});
?>
--EXPECTF--
release: Fiber stack allocate failed: %s

Fatal error: Uncaught Async\CompositeException in %s:%d
Stack trace:
#0 [internal function]: {closure:%s}()
#1 {main}
  thrown in %s on line %d
