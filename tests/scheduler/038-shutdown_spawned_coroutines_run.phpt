--TEST--
Coroutines spawned by a shutdown function and by a destructor at shutdown keep their arguments and run
--FILE--
<?php
use function Async\spawn;

class Late
{
    public function __destruct()
    {
        echo "destructor\n";
        $GLOBALS['late'] = spawn(function ($word) { echo "late coroutine: $word\n"; }, "argument");
    }
}

register_shutdown_function(function () {
    echo "shutdown function\n";
    spawn(function ($word) { echo "shutdown coroutine: $word\n"; }, "argument");
});

$late = new Late();
echo "main end\n";
?>
--EXPECTF--
main end
shutdown function
destructor
shutdown coroutine: argument
late coroutine: argument
