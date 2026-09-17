using System.Text.RegularExpressions;
using Initiator.Domain.States;

namespace Initiator.Tests;

/// <summary>
/// Reads the transition table out of the explosion node's firmware and asserts
/// that <see cref="NodeStateMachine"/> agrees with it, cell by cell.
/// </summary>
/// <remarks>
/// <para>
/// The rules exist twice — C# here, C++ there — and there is no way to have
/// only one copy: one runs on a server and the other on a board with no .NET.
/// What there can be is a build that fails when they drift, which is this.
/// </para>
/// <para>
/// Without it the two would diverge silently and the symptom would be a node
/// that refuses a command the dashboard offered, or offers one the node
/// refuses — in the field, months later, and blamed on the radio.
/// </para>
/// <para>
/// This is the only cross-project file access in the build, and it is read-only.
/// </para>
/// </remarks>
public sealed class FirmwareTransitionTableContractTests
{
    private const string FirmwareRelativePath =
        "ESP32/initiator/explosion/lib/state/node_state.cpp";

    private const string BeginMarker = "// TRANSITION-TABLE-BEGIN";
    private const string EndMarker = "// TRANSITION-TABLE-END";

    /// <summary>Rows in the firmware table's order.</summary>
    private static readonly NodeState[] RowOrder =
        [NodeState.Safe, NodeState.Init, NodeState.Armed, NodeState.Fire];

    /// <summary>Columns in the firmware table's order.</summary>
    private static readonly CommandType[] ColumnOrder =
        [CommandType.Init, CommandType.Arm, CommandType.Fire, CommandType.Safe];

    private static readonly Dictionary<string, TransitionKind> Kinds = new()
    {
        ["KIND_MOVE"] = TransitionKind.Moved,
        ["KIND_NOOP"] = TransitionKind.NoOp,
        ["KIND_RESTART"] = TransitionKind.CountdownRestarted,
        ["KIND_REJECT"] = TransitionKind.Rejected,
    };

    private static readonly Dictionary<string, NodeState> States = new()
    {
        ["STATE_SAFE"] = NodeState.Safe,
        ["STATE_INIT"] = NodeState.Init,
        ["STATE_ARMED"] = NodeState.Armed,
        ["STATE_FIRE"] = NodeState.Fire,
    };

    [Fact]
    public void The_firmware_table_and_the_backend_table_agree()
    {
        var cells = ParseFirmwareTable();

        Assert.Equal(RowOrder.Length * ColumnOrder.Length, cells.Count);

        var index = 0;

        foreach (var from in RowOrder)
        {
            foreach (var command in ColumnOrder)
            {
                var (kind, next) = cells[index++];
                var ours = NodeStateMachine.Apply(from, command);

                Assert.True(
                    ours.Kind == kind && ours.State == next,
                    $"{from} + {command}: the firmware says {kind} -> {next}, " +
                    $"this build says {ours.Kind} -> {ours.State}. " +
                    "One of the two tables has been changed without the other.");
            }
        }
    }

    [Fact]
    public void Both_sides_agree_on_the_boot_state()
    {
        var source = ReadFirmwareSource("lib/state/node_state.h");

        // Not a style point. A node that restores ARMED or FIRE from flash on
        // boot is the failure the whole design is arranged to prevent.
        Assert.Contains("#define STATE_BOOT STATE_SAFE", source, StringComparison.Ordinal);
        Assert.Equal(NodeState.Safe, NodeStateMachine.BootState);
    }

    [Fact]
    public void Both_sides_agree_on_the_wire_numbers()
    {
        var source = ReadFirmwareSource("lib/state/node_state.h");

        // These travel in the LoRa frame and in every HTTP body. A renumbering
        // on one side turns every stored record into a different story.
        AssertEnumValue(source, "STATE_SAFE", (int)NodeState.Safe);
        AssertEnumValue(source, "STATE_INIT", (int)NodeState.Init);
        AssertEnumValue(source, "STATE_ARMED", (int)NodeState.Armed);
        AssertEnumValue(source, "STATE_FIRE", (int)NodeState.Fire);

        AssertEnumValue(source, "COMMAND_INIT", (int)CommandType.Init);
        AssertEnumValue(source, "COMMAND_ARM", (int)CommandType.Arm);
        AssertEnumValue(source, "COMMAND_FIRE", (int)CommandType.Fire);
        AssertEnumValue(source, "COMMAND_SAFE", (int)CommandType.Safe);

        AssertEnumValue(source, "REASON_OK", (int)RejectReason.Ok);
        AssertEnumValue(source, "REASON_BAD_TRANSITION", (int)RejectReason.BadTransition);
        AssertEnumValue(source, "REASON_BAD_TARGET", (int)RejectReason.BadTarget);
        AssertEnumValue(source, "REASON_REPLAY", (int)RejectReason.Replay);
        AssertEnumValue(source, "REASON_BAD_COMMAND", (int)RejectReason.BadCommand);

        AssertEnumValue(source, "SOURCE_LORA", (int)CommandSource.Lora);
        AssertEnumValue(source, "SOURCE_API", (int)CommandSource.Api);
        AssertEnumValue(source, "SOURCE_TIMER", (int)CommandSource.Timer);
    }

    private static void AssertEnumValue(string source, string name, int expected)
    {
        var match = Regex.Match(source, $@"\b{Regex.Escape(name)}\s*=\s*(\d+)");

        Assert.True(match.Success, $"{name} is not defined with an explicit value in the firmware.");
        Assert.True(
            int.Parse(match.Groups[1].Value) == expected,
            $"{name} is {match.Groups[1].Value} in the firmware and {expected} here.");
    }

    private static List<(TransitionKind Kind, NodeState Next)> ParseFirmwareTable()
    {
        var source = ReadFirmwareSource("lib/state/node_state.cpp");

        var start = source.IndexOf(BeginMarker, StringComparison.Ordinal);
        var end = source.IndexOf(EndMarker, StringComparison.Ordinal);

        Assert.True(
            start >= 0 && end > start,
            $"Could not find the {BeginMarker} / {EndMarker} block. If the firmware's " +
            "table was reshaped, this parser has to be reshaped with it — do not just " +
            "delete the test.");

        var block = source[start..end];

        return [.. Regex.Matches(block, @"\{\s*(KIND_\w+)\s*,\s*(STATE_\w+)\s*\}")
            .Select(match =>
            {
                var kindText = match.Groups[1].Value;
                var stateText = match.Groups[2].Value;

                Assert.True(Kinds.ContainsKey(kindText), $"Unknown transition kind {kindText}.");
                Assert.True(States.ContainsKey(stateText), $"Unknown state {stateText}.");

                return (Kinds[kindText], States[stateText]);
            })];
    }

    private static string ReadFirmwareSource(string relativePathInProject)
    {
        var path = Path.Combine(FindRepositoryRoot(), "ESP32", "initiator", "explosion",
            relativePathInProject.Replace('/', Path.DirectorySeparatorChar));

        Assert.True(
            File.Exists(path),
            $"The firmware source is not where this test expects it: {path}. " +
            $"It should be at {FirmwareRelativePath} relative to the repository root.");

        return File.ReadAllText(path);
    }

    /// <summary>
    /// Walks up from the test assembly until it finds the directory holding both
    /// the backend and the firmware. Beats a pile of <c>..\</c> that breaks the
    /// moment the target framework or the output layout changes.
    /// </summary>
    private static string FindRepositoryRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);

        while (directory is not null)
        {
            if (Directory.Exists(Path.Combine(directory.FullName, "ESP32")) &&
                Directory.Exists(Path.Combine(directory.FullName, "Backend")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException(
            "Could not find the repository root (a directory containing both ESP32 and Backend) " +
            $"walking up from {AppContext.BaseDirectory}.");
    }
}
