--TEST--
S3.7 item 14: await(spawn(...)) with no variable holding the coroutine still returns its result
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\suspend;

var_dump(await(spawn(function() {
    suspend();
    return "kept in main";
})));

$waiter = spawn(function() {
    return await(spawn(function() {
        suspend();
        suspend();
        return "kept in spawned";
    }));
});

var_dump(await($waiter));
?>
--EXPECT--
string(12) "kept in main"
string(15) "kept in spawned"
