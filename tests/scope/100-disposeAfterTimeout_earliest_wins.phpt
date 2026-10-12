--TEST--
Scope: of several disposeAfterTimeout() calls the earliest deadline cancels; a negative timeout is refused, 0 cancels at the next poll
--DESCRIPTION--
A delay armed after the deadlines and longer than the earlier ones ends after them: the reactor runs due timers nearest
first. Had a later deadline won, the members' lines would come after its line, or not at all.
--FILE--
<?php
use Async\Coroutine;
use Async\Scope;
use function Async\await;
use function Async\delay;

function member(Scope $scope, string $name): Coroutine
{
    return $scope->spawn(function () use ($name) {
        try {
            delay(2000);
        } catch (Async\AsyncCancellation $e) {
            echo $name, ": ", $e->getMessage(), "\n";
        }
    });
}

$members = [];
$later = Scope::inherit()->asNotSafely();
$members[] = member($later, "shorter second");
$earlier = Scope::inherit()->asNotSafely();
$members[] = member($earlier, "longer second");
$zero = Scope::inherit()->asNotSafely();
$members[] = member($zero, "zero");
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

$members[] = Async\spawn(function () {
    delay(1000);
    echo "a delay armed after the deadlines: ended\n";
});

foreach ($members as $member) {
    await($member);
}

echo "end\n";
?>
--EXPECT--
Async\Scope::disposeAfterTimeout(): Argument #1 ($timeout) must be greater than or equal to 0
zero: Scope has been disposed due to timeout
shorter second: Scope has been disposed due to timeout
longer second: Scope has been disposed due to timeout
a delay armed after the deadlines: ended
end
