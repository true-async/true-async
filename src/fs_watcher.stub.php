<?php

/** @generate-class-entries */

namespace Async;

/**
 * One change a FileSystemWatcher saw.
 *
 * @strict-properties
 * @not-serializable
 */
final readonly class FileSystemEvent
{
    /** The path passed to the watcher. */
    public string $path;

    /**
     * The changed entry, relative to $path for a recursive watch ("sub/deep/config.php"); the basename of
     * $path for a change of the watched entry itself; null for a collapsed debounce event or when changes
     * were lost (an overflow of the kernel's queue).
     */
    public ?string $filename;

    /** The entry was created, deleted or moved. */
    public bool $renamed;

    /** The entry's contents or attributes changed. */
    public bool $changed;
}

/**
 * Watches a file or a directory from its construction until close() or its destruction, buffers the
 * changes and hands them out to foreach. Supported on Linux.
 *
 * @strict-properties
 * @not-serializable
 */
final class FileSystemWatcher implements Awaitable, \IteratorAggregate
{
    /**
     * @param string $path The file or directory to watch.
     * @param bool $recursive Watch the subdirectories too: one watch per directory, new ones added and
     *   removed ones dropped as they come and go, symlinks not followed, at most 8192 directories.
     * @param bool $coalesce true: one buffered event per changed entry, its flags merged; false: every
     *   change separately.
     * @param int $debounceMs 0, or the milliseconds of quiet after which a burst of changes is delivered
     *   as one event with $filename = null.
     * @param int $maxHoldMs 0, or the longest a debounced burst waits after its first change.
     * @param array $extensions Case-insensitive extensions of the entries to report, "php" or ".php";
     *   empty: every entry.
     */
    public function __construct(
        string $path,
        bool $recursive = false,
        bool $coalesce = true,
        int $debounceMs = 0,
        int $maxHoldMs = 0,
        array $extensions = []
    ) {}

    /** Stops watching and ends every foreach once the buffered events are taken; idempotent. */
    public function close(): void {}

    public function isClosed(): bool {}

    /** Yields FileSystemEvent objects; suspends while none is buffered and the watcher is open. */
    public function getIterator(): \Iterator {}
}
