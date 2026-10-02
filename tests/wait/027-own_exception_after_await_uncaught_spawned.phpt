--TEST--
S3.7 edge case: an uncaught exception a spawned waiter throws itself after await() returned ends the request as uncaught
--FILE--
<?php

use function Async\spawn;
use function Async\await;

// Neither coroutine is held by a variable; only the waiter's own exception is uncaught.
spawn(function() {
    $value = await(spawn(function() {
        return "fine";
    }));
    echo "waiter got $value\n";
    throw new LogicException("own failure");
});

echo "end of main\n";
?>
--EXPECTF--
end of main
waiter got fine
%AFatal error: Uncaught LogicException: own failure in %s:%d
%A
