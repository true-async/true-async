<?php

/** @generate-class-entries */

namespace Async;

/**
 * The process's and the host's CPU counters at one moment. Every *Ns field is a nanosecond counter with an
 * arbitrary origin: only the difference between two snapshots means something.
 *
 * @strict-properties
 * @not-serializable
 */
final class CpuSnapshot
{
    /** Monotonic wall time, the clock of hrtime(). */
    public readonly int $wallNs;

    /** User-mode CPU time of every thread of this process. */
    public readonly int $processUserNs;

    /** Kernel-mode CPU time of every thread of this process. */
    public readonly int $processSystemNs;

    /** Idle time of every CPU of the host (idle and iowait); 0 where the host's counters cannot be read. */
    public readonly int $systemIdleNs;

    /** Busy time of every CPU of the host (user, nice, system, irq, softirq, steal); 0 where unreadable. */
    public readonly int $systemBusyNs;

    /** The host's online CPUs. */
    public readonly int $cpuCount;

    private function __construct() {}

    public static function now(): CpuSnapshot {}
}
