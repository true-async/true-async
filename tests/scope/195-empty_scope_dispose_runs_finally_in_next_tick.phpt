--TEST--
Scope: disposing an empty scope runs its finally handlers in the next tick
--FILE--
<?php

use Async\Scope;
use function Async\suspend;

$disposed = new Scope();
$disposed->finally(function () {
    echo "disposed finally\n";
});
$disposed->dispose();
echo "after dispose\n";

suspend();
echo "end\n";

?>
--EXPECT--
after dispose
disposed finally
end
