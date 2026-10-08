--TEST--
Context: a running scope whose context holds its object survives gc_collect_cycles(); a second run collects it once the last member ends
--FILE--
<?php

use function Async\current_context;
use function Async\suspend;

$is_released = false;
$context = null;

$scope = new Async\Scope();
$member = $scope->spawn(function () {
    $GLOBALS['context'] = current_context();
    try {
        while (!$GLOBALS['is_released']) {
            suspend();
        }
        // No current_context() here: its temporary would put the context back in the GC's root buffer.
        echo "member: finished\n";
    } catch (Async\AsyncCancellation $e) {
        echo "member: cancelled\n";
    }
});
while ($context === null) {
    suspend();
}

// Only the scope's context holds the object, and only the member reaches the context.
$context->set('scope', $scope);
$weak_scope = WeakReference::create($scope);
unset($scope, $context);

gc_collect_cycles();
echo "alive while running: ", var_export($weak_scope->get() !== null, true), "\n";

$is_released = true;
suspend();
suspend();
unset($member);

gc_collect_cycles();
echo "alive after: ", var_export($weak_scope->get() !== null, true), "\n";

?>
--EXPECT--
alive while running: true
member: finished
alive after: false
