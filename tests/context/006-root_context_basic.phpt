--TEST--
root_context() basic usage and no memory leak
--XFAIL--
Not implemented yet: S9.12 of dev/PLAN.md
--FILE--
<?php

$ctx = Async\root_context();
var_dump($ctx instanceof Async\Context);

// Same object on second call
$ctx2 = Async\root_context();
var_dump($ctx === $ctx2);

echo "done\n";
?>
--EXPECT--
bool(true)
bool(true)
done
