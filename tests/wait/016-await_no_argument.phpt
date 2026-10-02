--TEST--
S3.7 item 10: await() without an argument throws an ArgumentCountError
--FILE--
<?php

use function Async\await;

try {
    await();
    echo "no exception\n";
} catch (Throwable $e) {
    echo $e instanceof ArgumentCountError ? "ArgumentCountError" : get_class($e), "\n";
}

echo "end\n";
?>
--EXPECT--
ArgumentCountError
end
