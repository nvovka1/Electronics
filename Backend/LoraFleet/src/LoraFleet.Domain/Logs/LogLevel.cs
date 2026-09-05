namespace LoraFleet.Domain.Logs;

/// <summary>
/// Mirrors LogLevel in src/core/log.h. The numbers are protocol: they arrive
/// over the wire, so they can never be reordered here for convenience.
/// </summary>
public enum LogLevel : byte
{
    /// <summary>Cannot continue.</summary>
    Panic = 0,

    /// <summary>The device is not doing its job.</summary>
    Error = 1,

    /// <summary>Working, but worse than it should.</summary>
    Warn = 2,

    /// <summary>A state change worth remembering.</summary>
    Info = 3,

    /// <summary>Detail. Not present at all in a field image.</summary>
    Debug = 4,

    /// <summary>Step by step. Not present at all in a field image.</summary>
    Trace = 5,
}
