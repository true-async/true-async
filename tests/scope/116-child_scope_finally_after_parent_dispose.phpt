--TEST--
Disposing a scope runs its finally handlers; those of an idle child scope run when the request ends, with null
--FILE--
<?php

use Async\Scope;
use function Async\await;
use function Async\suspend;

$parent = new Scope();
$child = Scope::inherit($parent);
$parent->finally(function (?Scope $scope) {
    echo "parent finally: ", $scope === null ? 'null' : 'scope', "\n";
});
$child->finally(function (?Scope $scope) {
    echo "child finally: ", $scope === null ? 'null' : 'scope', "\n";
});
await($child->spawn(fn() => 1));

$parent->dispose();
echo "after dispose\n";
suspend();
echo "end\n";

?>
--EXPECT--
after dispose
parent finally: scope
end
child finally: null
