<?php
// B4: a recursive chain await(spawn(f, k - 1)) (dev/plans/S3.md, section 12).
// Usage: b4.php <depth> <chains>. One operation is one link: spawn, park, finish, wake.
use function Async\spawn;
use function Async\await;

$depth = (int) ($argv[1] ?? 100);
$chains = (int) ($argv[2] ?? 1000);
$link = static function (int $k) use (&$link) {
    return $k === 0 ? 0 : await(spawn($link, $k - 1)) + 1;
};

for ($chain = 0; $chain < $chains; $chain++) {
    await(spawn($link, $depth));
}
