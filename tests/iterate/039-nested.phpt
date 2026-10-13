--TEST--
iterate(): nested in a callback of iterate()
--FILE--
<?php

use function Async\iterate;
use function Async\suspend;

iterate([1, 2], function ($outer) {
    iterate(['a', 'b'], function ($inner) use ($outer) {
        suspend();
        echo "$outer$inner\n";
    });
    echo "outer $outer done\n";
}, concurrency: 1);

echo "done\n";
?>
--EXPECT--
1a
1b
outer 1 done
2a
2b
outer 2 done
done
