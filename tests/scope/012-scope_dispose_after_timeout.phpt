--TEST--
Scope: disposeAfterTimeout() - basic usage
--XFAIL--
Not implemented yet: S9.5 of dev/PLAN.md
--FILE--
<?php

use Async\Scope;

$scope = new Scope();

$scope->disposeAfterTimeout(1000);

echo "Dispose after timeout executed successfully\n";

?>
--EXPECT--
Dispose after timeout executed successfully