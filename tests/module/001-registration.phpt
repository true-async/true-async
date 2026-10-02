--TEST--
true_async registers its module, version and INI switch
--FILE--
<?php
var_dump(extension_loaded('true_async'));
var_dump(preg_match('/^\d+\.\d+\.\d+(-dev)?$/', phpversion('true_async')));
var_dump(ini_get('true_async.enable'));
var_dump(array_keys((new ReflectionExtension('true_async'))->getINIEntries()));
?>
--EXPECT--
bool(true)
int(1)
string(1) "1"
array(2) {
  [0]=>
  string(17) "true_async.enable"
  [1]=>
  string(25) "true_async.debug_deadlock"
}
