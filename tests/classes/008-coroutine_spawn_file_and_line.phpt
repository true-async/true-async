--TEST--
Coroutine::getSpawnFileAndLine() gives the file and the line of the spawn() call
--FILE--
<?php
use function Async\spawn;

$coroutine = spawn(function () {});
[$file, $line] = $coroutine->getSpawnFileAndLine();
var_dump($file === __FILE__, $line);
?>
--EXPECT--
bool(true)
int(4)
