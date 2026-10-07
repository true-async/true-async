--TEST--
true_async prints its phpinfo() section
--FILE--
<?php
(new ReflectionExtension('true_async'))->info();
?>
--EXPECTF--
true_async

true_async support => enabled
Version => %d.%d.%s

Directive => Local Value => Master Value
true_async.enable => 1 => 1
true_async.debug_deadlock => On => On
true_async.partial_deadlock => report => report
true_async.partial_deadlock_interval => 5000 => 5000
