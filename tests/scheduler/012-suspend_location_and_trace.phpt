--TEST--
getSuspendFileAndLine(), getSuspendLocation() and getTrace() read the frame a coroutine parked in
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;
use function Async\current_coroutine;

function park() {
    suspend();
}

$coroutine = spawn(function () {
    park();
});

// Not started yet: nothing to read.
var_dump($coroutine->getSuspendFileAndLine(), $coroutine->getSuspendLocation(), $coroutine->getTrace());

// The coroutine runs to its yield inside park() and main runs again.
suspend();

[$file, $line] = $coroutine->getSuspendFileAndLine();
var_dump(basename($file), $line);
var_dump(basename($coroutine->getSuspendLocation()));

$trace = $coroutine->getTrace(DEBUG_BACKTRACE_IGNORE_ARGS);
var_dump($trace[0]['function'], $trace[0]['line'], $trace[1]['function']);

// The running coroutine has no suspend location (D18).
var_dump(current_coroutine()->getSuspendLocation(), current_coroutine()->getTrace());

// It resumes and finishes.
suspend();
var_dump($coroutine->isCompleted(), $coroutine->getSuspendFileAndLine(), $coroutine->getTrace());
?>
--EXPECTF--
array(2) {
  [0]=>
  NULL
  [1]=>
  int(0)
}
string(7) "unknown"
NULL
string(%d) "012-suspend_location_and_trace.php"
int(7)
string(%d) "012-suspend_location_and_trace.php:7"
string(13) "Async\suspend"
int(7)
string(4) "park"
string(7) "unknown"
NULL
bool(true)
array(2) {
  [0]=>
  NULL
  [1]=>
  int(0)
}
NULL
