--TEST--
true_async.partial_deadlock_interval: 5000 by default, 0 and 1000 accepted, 1 to 999, a negative, an empty and a non-numeric value refused without a warning; a bare off in php.ini does not mean 0
--INI--
true_async.partial_deadlock_interval=off
--FILE--
<?php
echo ini_get('true_async.partial_deadlock_interval'), "\n";

foreach (['999', '1', '-1', '', 'never', '5s', '1000', '0'] as $value) {
    echo $value, ": ", var_export(ini_set('true_async.partial_deadlock_interval', $value) !== false, true),
        ", now ", ini_get('true_async.partial_deadlock_interval'), "\n";
}
?>
--EXPECT--
5000
999: false, now 5000
1: false, now 5000
-1: false, now 5000
: false, now 5000
never: false, now 5000
5s: false, now 5000
1000: true, now 1000
0: true, now 0
