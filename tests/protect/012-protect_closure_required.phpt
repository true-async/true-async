--TEST--
Async\protect: closure parameter is required
--XFAIL--
Not implemented yet: S3.8 of dev/PLAN.md
--FILE--
<?php

use function Async\protect;

echo "start\n";

// Test with no parameters
try {
    protect();
} catch (ArgumentCountError $e) {
    echo "caught ArgumentCountError: " . $e->getMessage() . "\n";
}

// Test with too many parameters
try {
    protect(function() {}, "extra param");
} catch (ArgumentCountError $e) {
    echo "caught ArgumentCountError for too many params\n";
}

echo "end\n";

?>
--EXPECTF--
start
caught ArgumentCountError: %s
caught ArgumentCountError for too many params
end