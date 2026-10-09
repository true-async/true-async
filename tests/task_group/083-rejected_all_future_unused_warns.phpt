--TEST--
TaskGroup: a rejected all() Future nobody awaits warns at its release
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->spawn(function () {
    throw new RuntimeException("boom");
});
$group->close();

$future = $group->all();
$group->all(ignoreErrors: true)->await();
unset($future);
echo "end\n";
?>
--EXPECTF--
Warning: Future was never used; call await(), map(), catch(), finally() or ignore() to suppress this warning. Created at %s:11 in Unknown on line %d

Warning: Unhandled exception in Future: ; use catch() or ignore() to handle. Created at %s:11 in Unknown on line %d
end
