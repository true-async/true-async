--TEST--
Scope: of several disposeAfterTimeout() calls the earliest deadline cancels; a negative timeout is refused, 0 cancels at the next poll
--FILE--
<?php
use Async\Scope;
use function Async\delay;

function member(Scope $scope, string $name): void
{
    $scope->spawn(function () use ($name) {
        $start = hrtime(true);
        try {
            delay(2000);
        } catch (Async\AsyncCancellation $e) {
            echo $name, ": ", $e->getMessage(), " before 1000 ms: ", var_export(hrtime(true) - $start < 1000e6, true), "\n";
        }
    });
}

$later = Scope::inherit()->asNotSafely();
member($later, "shorter second");
$earlier = Scope::inherit()->asNotSafely();
member($earlier, "longer second");
$zero = Scope::inherit()->asNotSafely();
member($zero, "zero");
Async\suspend();

$later->disposeAfterTimeout(5000);
$later->disposeAfterTimeout(20);
$earlier->disposeAfterTimeout(20);
$earlier->disposeAfterTimeout(5000);
$zero->disposeAfterTimeout(0);

try {
    $zero->disposeAfterTimeout(-1);
} catch (ValueError $e) {
    echo $e->getMessage(), "\n";
}

delay(100);
echo "end\n";
?>
--EXPECT--
Async\Scope::disposeAfterTimeout(): Argument #1 ($timeout) must be greater than or equal to 0
zero: Scope has been disposed due to timeout before 1000 ms: true
shorter second: Scope has been disposed due to timeout before 1000 ms: true
longer second: Scope has been disposed due to timeout before 1000 ms: true
end
