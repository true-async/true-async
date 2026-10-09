--TEST--
TaskSet: a rejected joinAny() takes the failures its CompositeException carries
--FILE--
<?php

use Async\TaskSet;

$set = new TaskSet();
$set->spawn(function () {
    throw new RuntimeException("first");
});
$set->spawn(function () {
    throw new RuntimeException("second");
});
$set->close();

try {
    $set->joinAny()->await();
} catch (Async\CompositeException $error) {
    echo count($error->getExceptions()), " errors\n";
}

var_dump(count($set));
?>
--EXPECT--
2 errors
int(0)
