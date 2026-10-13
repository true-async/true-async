--TEST--
Scope: awaitCompletion() returns at once on a closed scope
--FILE--
<?php

use Async\Scope;

$closed = new Scope();
$closed->dispose();
$closed->awaitCompletion(Async\timeout(1000));
echo "closed: returned\n";

?>
--EXPECT--
closed: returned
