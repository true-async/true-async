--TEST--
Async\signal(): exit() ends the script while a Future nobody awaits holds a watch
--FILE--
<?php
use Async\Signal;
use function Async\delay;
use function Async\signal;

$held = signal(Signal::SIGUSR1);
$held->ignore();
delay(1);
echo "exit\n";
exit(0);
?>
--EXPECT--
exit
