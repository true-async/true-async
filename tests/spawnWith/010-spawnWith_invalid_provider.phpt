--TEST--
Async\spawnWith: invalid provider parameter
--XFAIL--
Not implemented yet: S9.2 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn_with;

echo "start\n";

// Test with non-ScopeProvider parameter
try {
    spawn_with("not a provider", function() {});
} catch (TypeError $e) {
    echo "caught TypeError for string: " . $e->getMessage() . "\n";
}

// Test with array
try {
    spawn_with([], function() {});
} catch (TypeError $e) {
    echo "caught TypeError for array\n";
}

// Test with stdClass
try {
    spawn_with(new stdClass(), function() {});
} catch (TypeError $e) {
    echo "caught TypeError for stdClass\n";
}

echo "end\n";

?>
--EXPECTF--
start
caught TypeError for string: %s
caught TypeError for array
caught TypeError for stdClass
end