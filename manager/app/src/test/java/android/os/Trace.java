package android.os;

// Compose runtime tracing has no platform backend in local JVM tests.
public final class Trace {
    private Trace() {}

    public static void beginSection(String sectionName) {}

    public static void endSection() {}
}
