--TEST--
TaskSet: a pending joinAny() takes the first successful task when it ends, leaving the earlier failure in the set
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskSet;

$gate = new FutureState();
$set = new TaskSet();
$set->spawn(function () {
    throw new RuntimeException("failed");
});
$set->spawn(fn() => (new Future($gate))->await());

$any = $set->joinAny();
$gate->complete("success");

var_dump($any->await());
var_dump(count($set));

try {
    $set->joinNext()->await();
} catch (RuntimeException $error) {
    echo "left: ", $error->getMessage(), "\n";
}
?>
--EXPECT--
string(7) "success"
int(1)
left: failed
