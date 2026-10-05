--TEST--
An exit() in a shutdown function does not drop an exception nobody observed: it is still printed at the end
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;

$tasks = [spawn(function () { throw new RuntimeException("lost"); })];
suspend();
register_shutdown_function(function () {
    echo "shutdown function\n";
    exit(0);
});
echo "end\n";
?>
--EXPECTF--
end
shutdown function

Fatal error: Uncaught RuntimeException: lost in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d
