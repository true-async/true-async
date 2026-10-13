--TEST--
A finally handler added to a closed scope runs when its object dies
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

suspend();
echo "end\n";

?>
--EXPECT--
closed: true, cancelled: false
unset
after unset
late finally, scope: NULL
end
