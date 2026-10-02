--TEST--
S3.7 item 8: when A awaits B and B awaits C, C's result reaches B and B's return value reaches A
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\suspend;

$c = spawn(function() {
    echo "C runs\n";
    suspend();
    echo "C returns 1\n";
    return 1;
});
$b = spawn(function() use ($c) {
    echo "B awaits C\n";
    $value = await($c);
    echo "B got $value\n";
    return $value + 10;
});
$a = spawn(function() use ($b) {
    echo "A awaits B\n";
    $value = await($b);
    echo "A got $value\n";
    return $value + 100;
});

// Queue [C, B, A]; C's suspend() lets B and A park before C returns.
// B returns 1 + 10 = 11, A returns 11 + 100 = 111.
echo "main awaits A\n";
var_dump(await($a));
?>
--EXPECT--
main awaits A
C runs
B awaits C
A awaits B
C returns 1
B got 1
A got 11
int(111)
