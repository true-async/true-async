--TEST--
Scope: 10 child scopes leave their parent newest first, each knowing its index in the parent's vector
--FILE--
<?php

use Async\Scope;

$parent = new Scope();
$scopes = [];

// The time linear in the children is benchmark B20 (bench/b20.php), not a test.
for ($i = 0; $i < 10; $i++) {
    $scopes[] = Scope::inherit($parent);
}

$survivors = [];

while ($scopes) {
    $scope = array_pop($scopes);

    if (count($scopes) % 3 === 0) {
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
