--TEST--
TaskGroup: suppressErrors() marks the failed tasks read, so the dropped group reports nothing
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->spawn(fn() => "ok");
$group->spawn(function () {
    throw new RuntimeException("suppressed");
});
$group->close();
$group->awaitCompletion();
$group->suppressErrors();
unset($group);

Async\suspend();
echo "end\n";
?>
--EXPECT--
end
