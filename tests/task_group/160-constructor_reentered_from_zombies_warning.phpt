--TEST--
TaskGroup: a warning handler that constructs the group through debug_backtrace() makes the outer constructor throw
--FILE--
<?php

use Async\Scope;
use Async\TaskGroup;

$zombies = new Scope();
$zombies->allowZombies();
$other = new Scope();

set_error_handler(function ($level, $message) use ($other) {
    echo "warning: $message\n";

    foreach (debug_backtrace(DEBUG_BACKTRACE_PROVIDE_OBJECT) as $frame) {
        if (($frame['object'] ?? null) instanceof TaskGroup) {
            $frame['object']->__construct(scope: $other);
            echo "constructed in the handler\n";
        }
    }

    return true;
});

try {
    $group = new TaskGroup(scope: $zombies);
} catch (Error $error) {
    echo get_class($error), ": ", $error->getMessage(), "\n";
}

restore_error_handler();
echo "end\n";
?>
--EXPECT--
warning: Async\TaskGroup::__construct(): TaskGroup cancels its tasks even though the Scope allows zombies
constructed in the handler
Error: Cannot call constructor twice
end
