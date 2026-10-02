--TEST--
Async\protect: nested protect calls
--XFAIL--
Not implemented yet: S3.8 of dev/PLAN.md
--FILE--
<?php

use function Async\protect;

echo "start\n";

protect(function() {
    echo "outer protect start\n";
    
    protect(function() {
        echo "inner protect\n";
    });
    
    echo "outer protect end\n";
});

echo "end\n";

?>
--EXPECT--
start
outer protect start
inner protect
outer protect end
end