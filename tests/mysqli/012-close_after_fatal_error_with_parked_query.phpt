--TEST--
A fatal error while a mysqli query is parked: a shutdown function closes the connection, and the request ends without an error (review B2)
--EXTENSIONS--
mysqli
--SKIPIF--
<?php
if (getenv('MYSQL_TEST_HOST') === false) {
    die('skip no MySQL fixture: tools/test.py names one in MYSQL_TEST_HOST');
}
?>
--FILE--
<?php
mysqli_report(MYSQLI_REPORT_OFF);
$mysqli = new mysqli(getenv('MYSQL_TEST_HOST'), getenv('MYSQL_TEST_USER'), getenv('MYSQL_TEST_PASSWD'),
    getenv('MYSQL_TEST_DB'), (int) getenv('MYSQL_TEST_PORT'));

register_shutdown_function(function () use ($mysqli) {
    echo "shutdown: close ";
    var_dump($mysqli->close());
});

Async\spawn(function () use ($mysqli) {
    $mysqli->query('SELECT SLEEP(5)');
    echo "never reached\n";
});

Async\spawn(function () {
    ini_set('memory_limit', '2M');
    $big = str_repeat('x', 4 * 1024 * 1024);
});
?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s
shutdown: close bool(true)
