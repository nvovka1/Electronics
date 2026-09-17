using Initiator.Domain.Commands;
using Initiator.Domain.Logs;
using Initiator.Domain.States;

namespace Initiator.Web.Models;

public sealed class CommandListViewModel
{
    public required IReadOnlyList<Command> Commands { get; init; }

    public required IReadOnlyList<string> KnownSerials { get; init; }

    public string? Serial { get; init; }

    public int Limit { get; init; }
}

public sealed class StateHistoryViewModel
{
    public required IReadOnlyList<StateEvent> Events { get; init; }

    public required IReadOnlyList<string> KnownSerials { get; init; }

    public string? Serial { get; init; }

    public int Limit { get; init; }
}

public sealed class LogListViewModel
{
    public required IReadOnlyList<LogRecord> Records { get; init; }

    public required IReadOnlyList<string> KnownSerials { get; init; }

    public string? Serial { get; init; }

    public byte? MaxLevel { get; init; }

    public byte? Tag { get; init; }

    public int Limit { get; init; }

    public long TotalMatching { get; init; }
}

public sealed class ErrorViewModel
{
    public string? RequestId { get; init; }

    public bool ShowRequestId => !string.IsNullOrEmpty(RequestId);
}
