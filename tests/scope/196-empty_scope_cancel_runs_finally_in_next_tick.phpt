--TEST--
Scope: cancelling an empty scope runs its finally handlers in the next tick
--FILE--
<?php

use Async\Scope;
use function Async\suspend;

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
after cancel
cancelled finally
end
