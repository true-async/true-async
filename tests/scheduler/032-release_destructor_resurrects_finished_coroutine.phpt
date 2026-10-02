--TEST--
A destructor run by the release of a finished coroutine's arguments may take the coroutine again, await it and keep it after the release
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\current_coroutine;

class Arg
{
    public function __destruct()
    {
        /* The finished coroutine is still current while its last reference goes. */
        $coroutine = current_coroutine();
        echo "destructor: completed ", var_export($coroutine->isCompleted(), true), "\n";
        var_dump(await($coroutine));
        $GLOBALS['kept'] = $coroutine;
    }
}

spawn(function ($arg) { echo "body\n"; return "result"; }, new Arg());
Async\suspend();
$kept = $GLOBALS['kept'];
echo "kept: completed ", var_export($kept->isCompleted(), true), "\n";
var_dump($kept->getResult());
?>
--EXPECTF--
body
destructor: completed true
string(6) "result"
kept: completed true
NULL
