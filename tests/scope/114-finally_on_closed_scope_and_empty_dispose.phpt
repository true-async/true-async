--TEST--
A finally handler added to a closed scope runs when its object dies; disposing an empty scope runs its handlers in the next tick
--FILE--
<?php

use Async\Scope;
use function Async\await;
use function Async\suspend;

$closed = new Scope();
await($closed->spawn(fn() => 1));
$closed->dispose();
echo "closed: ", var_export($closed->isClosed(), true), ", cancelled: ", var_export($closed->isCancelled(), true), "\n";
$closed->finally(function (?Scope $scope) {
    echo "late finally, scope: ", var_export($scope, true), "\n";
});
echo "unset\n";
unset($closed);
echo "after unset\n";

$disposed = new Scope();
$disposed->finally(function () {
    echo "disposed finally\n";
});
$disposed->dispose();
echo "after dispose\n";

$cancelled = new Scope();
$cancelled->finally(function () {
    echo "cancelled finally\n";
});
$cancelled->cancel();
echo "after cancel\n";

suspend();
echo "end\n";

?>
--EXPECT--
closed: true, cancelled: false
unset
after unset
after dispose
after cancel
cancelled finally
disposed finally
late finally, scope: NULL
end
