--TEST--
A destructor that throws when the drain releases a finished mapper ends the request with that exception
--FILE--
<?php
use Async\Future;

class Throws
{
    public function __destruct()
    {
        echo "destructor\n";
        throw new Exception("destructor");
    }
}

$captured = new Throws();

Future::completed(1)->map(function ($value) use ($captured) {
    echo "mapper $value\n";
    return $value;
})->ignore();

unset($captured);
Async\suspend();
echo "unreachable\n";
?>
--EXPECTF--
mapper 1
destructor

Fatal error: Uncaught Exception: destructor in %s:%d
Stack trace:
#0 [internal function]: Throws->__destruct()
#1 {main}
  thrown in %s on line %d
