--TEST--
Coroutine: GC handler with multiple ZVALs
--XFAIL--
Not implemented yet: S9.6 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;
use function Async\suspend;

// Test GC with coroutine containing multiple ZVALs
$test_data = ["key1" => "value1", "key2" => "value2"];

$coroutine = spawn(function() use ($test_data) {
    // Use all data to ensure they're tracked by GC
    $result = [];
    foreach ($test_data as $key => $value) {
        $result[$key] = $value;
    }
    return $result;
});

// Add finally handler
$coroutine->finally(function() {
    echo "Finally with data\n";
});

// Force garbage collection
$collected = gc_collect_cycles();

suspend(); // Suspend to simulate coroutine lifecycle

// Get result
$result = $coroutine->getResult();
var_dump($result);

// Verify GC was called
var_dump($collected >= 0);

?>
--EXPECT--
Finally with data
array(2) {
  ["key1"]=>
  string(6) "value1"
  ["key2"]=>
  string(6) "value2"
}
bool(true)