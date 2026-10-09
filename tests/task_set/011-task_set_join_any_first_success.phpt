--TEST--
TaskSet: joinAny() - returns first successful result, ignoring errors
--FILE--
<?php

use Async\TaskSet;
use function Async\spawn;
use function Async\suspend;

spawn(function() {
    $set = new TaskSet();

    $set->spawn(function() {
        throw new \RuntimeException("error1");
    });

    $set->spawn(function() {
        suspend();
        return "success";
    });

    $result = $set->joinAny()->await();
    echo "joinAny result: $result\n";
});
?>
--EXPECT--
joinAny result: success

Fatal error: Uncaught Async\CompositeException in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
