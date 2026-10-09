--TEST--
TaskGroup: a group made and dropped by an output handler at the request's end, after the coroutines are gone, calls no finally handler
--FILE--
<?php

use Async\TaskGroup;

ob_start(function (string $buffer) {
    $group = new TaskGroup();
    $group->finally(function () {
        echo "not reached\n";
    });
    unset($group);

    return $buffer . "output handler dropped the group\n";
});

echo "end of script\n";
?>
--EXPECT--
end of script
output handler dropped the group
