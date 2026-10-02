--TEST--
S3.7 item 10: await() of an object that is not a Completable, or of a scalar, throws a TypeError
--FILE--
<?php

use function Async\await;

$arguments = [
    "stdClass" => new stdClass(),
    "Closure" => function() {
        return 1;
    },
    "int" => 42,
    "float" => 1.5,
    "string" => "coroutine",
    "bool" => true,
];

foreach ($arguments as $label => $argument) {
    try {
        await($argument);
        echo "$label: no exception\n";
    } catch (Throwable $e) {
        echo "$label: ", $e instanceof TypeError ? "TypeError" : get_class($e), "\n";
    }
}

echo "end\n";
?>
--EXPECT--
stdClass: TypeError
Closure: TypeError
int: TypeError
float: TypeError
string: TypeError
bool: TypeError
end
