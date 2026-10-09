--TEST--
TaskGroup: a fatal error in one task while another runs and a finally handler waits ends the request cleanly
--SKIPIF--
<?php
if (getenv("USE_ZEND_ALLOC") === "0") {
    die("skip Zend MM disabled");
}
?>
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

register_shutdown_function(function () {
    echo "shutdown\n";
});

$group = new TaskGroup();
$group->finally(function () {
    echo "not reached\n";
});
$group->spawn(function () {
    (new Future(new FutureState()))->await();
});
$group->spawn(function () {
    eval('function twice() {} function twice() {}');
});

$group->all()->await();
?>
--EXPECTF--
Fatal error: Cannot redeclare function twice() (previously declared in %s(%d) : eval()'d code:1) in %s(%d) : eval()'d code on line %d
shutdown
