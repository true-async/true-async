--TEST--
Async\CompositeException keeps its list when unserialize() leaves a reference in the property
--FILE--
<?php
$payload = 'O:24:"Async\CompositeException":8:{'
    . 's:10:"' . "\0*\0" . 'message";s:1:"m";s:17:"' . "\0Exception\0" . 'string";s:0:"";'
    . 's:7:"' . "\0*\0" . 'code";i:0;s:7:"' . "\0*\0" . 'file";s:1:"f";s:7:"' . "\0*\0" . 'line";i:1;'
    . 's:16:"' . "\0Exception\0" . 'trace";a:1:{i:0;O:9:"Exception":0:{}}'
    . 's:19:"' . "\0Exception\0" . 'previous";N;'
    . 's:36:"' . "\0Async\\CompositeException\0" . 'exceptions";R:7;}';

$composite = unserialize($payload);
var_dump(count($composite->getExceptions()));

$composite->addException(new RuntimeException('b'));

foreach ($composite->getExceptions() as $exception) {
    echo get_class($exception), "\n";
}
?>
--EXPECT--
int(1)
Exception
RuntimeException
