--TEST--
Scope: 200 000 child scopes leave their parent newest first in linear time, each knowing its index in the parent's vector
--INI--
max_execution_time=20
memory_limit=1G
--FILE--
<?php

use Async\Scope;

$parent = new Scope();
$scopes = [];

for ($i = 0; $i < 200000; $i++) {
    $scopes[] = Scope::inherit($parent);
}

$survivors = [];

while ($scopes) {
    $scope = array_pop($scopes);

    if (count($scopes) % 50000 === 0) {
        $survivors[] = $scope;
    }
}

unset($scope);
var_dump(count($parent->getChildScopes()));
$survivors = [];
var_dump(count($parent->getChildScopes()));

?>
--EXPECT--
int(4)
int(0)
