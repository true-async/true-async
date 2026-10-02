--TEST--
Awaiting a finished coroutine gives its result, or throws the same exception object every time, and the awaited exception does not end the request
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$failing = spawn(function () {
    throw new RuntimeException("failed");
});

$returning = spawn(function () {
    return [1, 2];
});

suspend();

var_dump($failing->isCompleted(), $returning->isCompleted());
var_dump(await($returning));

$caught = [];

for ($i = 0; $i < 2; $i++) {
    try {
        await($failing);
    } catch (RuntimeException $exception) {
        $caught[] = $exception;
    }
}

var_dump($caught[0]->getMessage(), $caught[0] === $caught[1], $caught[0] === $failing->getException());
echo "end\n";
?>
--EXPECT--
bool(true)
bool(true)
array(2) {
  [0]=>
  int(1)
  [1]=>
  int(2)
}
string(6) "failed"
bool(true)
bool(true)
end
