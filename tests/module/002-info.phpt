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
