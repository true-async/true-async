--TEST--
Future: a Future never constructed (unserialize()) has no state
--FILE--
<?php

use Async\AsyncException;

$empty = unserialize('O:12:"Async\Future":0:{}');
var_dump($empty->isCompleted(), $empty->isCancelled(), $empty->getAwaitingInfo(), $empty->getCreatedLocation());

foreach (['await', 'cancel'] as $method) {
    try {
        $empty->$method();
    } catch (AsyncException $e) {
        echo $method, ": ", $e->getMessage(), "\n";
    }
}

try {
    $empty->map(fn() => 1);
} catch (AsyncException $e) {
    echo "map: ", $e->getMessage(), "\n";
}

try {
    Async\await($empty);
} catch (AsyncException $e) {
    echo "await(): ", $e->getMessage(), "\n";
}

?>
--EXPECT--
bool(false)
bool(false)
array(0) {
}
string(7) "unknown"
await: Future has no state
cancel: Future has no state
map: Future has no state
await(): Future has no state
