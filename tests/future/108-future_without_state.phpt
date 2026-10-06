--TEST--
Future: a clone is refused, and a Future never constructed (unserialize()) has no state
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use Async\AsyncException;

$future = Future::completed(1);
$future->ignore();

try {
    clone $future;
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}

$state = new FutureState();
$state->ignore();

try {
    clone $state;
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}

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

$first = new FutureState();
$first->ignore();
$second = new FutureState();
$reconstructed = new Future($first);
$reconstructed->__construct($second);
$second->complete("second state");
echo $reconstructed->await(), "\n";

?>
--EXPECT--
Trying to clone an uncloneable object of class Async\Future
Trying to clone an uncloneable object of class Async\FutureState
bool(false)
bool(false)
array(0) {
}
string(7) "unknown"
await: Future has no state
cancel: Future has no state
map: Future has no state
await(): Future has no state
second state
