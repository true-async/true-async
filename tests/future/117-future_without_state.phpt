--TEST--
A Future with no state (unserialize()) refuses as a token of await() and of await_all() and reports no location
--FILE--
<?php
use Async\Future;
use function Async\await_all;

$empty = unserialize('O:12:"Async\Future":0:{}');

/* Refused before the outcome is read, where TrueAsync returns it; each future is marked used before
 * its token is read, as in TrueAsync, so neither warns. */
try {
    Future::completed(1)->await($empty);
} catch (Throwable $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

try {
    Async\await(new Future(new Async\FutureState()), $empty);
} catch (Throwable $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

$item = Future::completed(2);
$item->ignore();

try {
    await_all([$item], $empty);
} catch (Throwable $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

var_dump($empty->getCreatedFileAndLine(), $empty->getCompletedFileAndLine(), $empty->getCompletedLocation());
?>
--EXPECT--
Async\AsyncException: Future has no state
Async\AsyncException: Future has no state
Async\AsyncException: Future has no state
array(2) {
  [0]=>
  NULL
  [1]=>
  int(0)
}
array(2) {
  [0]=>
  NULL
  [1]=>
  int(0)
}
string(7) "unknown"
