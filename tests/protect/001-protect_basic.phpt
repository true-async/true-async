--TEST--
Async\protect: basic usage
--XFAIL--
Not implemented yet: S3.8 of dev/PLAN.md
--FILE--
<?php

use function Async\protect;

echo "start\n";

protect(function() {
    echo "protected block\n";
});

echo "end\n";

?>
--EXPECT--
start
protected block
end