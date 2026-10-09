--TEST--
TaskGroup: foreach iteration yields results as [result, error]
--XFAIL--
Not implemented yet: S9.29 of dev/PLAN.md
--FILE--
<?php

use Async\TaskGroup;
use function Async\spawn;

spawn(function() {
    $group = new TaskGroup();

    $group->spawnWithKey("a", function() { return "first"; });
    $group->spawnWithKey("b", function() { return "second"; });
    $group->spawnWithKey("c", function() { return "third"; });

    $group->close();

    foreach ($group as $key => $pair) {
        [$result, $error] = $pair;
        echo "$key => result=$result error=" . ($error === null ? "null" : $error->getMessage()) . "\n";
    }

    echo "iteration done\n";
});
?>
--EXPECT--
a => result=first error=null
b => result=second error=null
c => result=third error=null
iteration done
