--TEST--
Future: no warning when exception is ignored
--XFAIL--
Not implemented yet: S5.2 of dev/PLAN.md
--FILE--
<?php

use Async\FutureState;
use Async\Future;

$state = new FutureState();
$future = new Future($state);
$future->ignore();
$state->error(new Exception("Test error"));

unset($future, $state);

echo "Done\n";

?>
--EXPECT--
Done
