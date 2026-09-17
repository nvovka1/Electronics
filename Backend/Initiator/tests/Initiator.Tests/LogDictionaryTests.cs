using Initiator.Domain.Logs;
using Initiator.Domain.States;

namespace Initiator.Tests;

/// <summary>
/// The dictionary is a contract with the firmware's LogCode enum. These tests do
/// not check that the numbers are right — nothing here can know that — but they
/// do check that the decoding does what the codes say, which is the part that
/// silently produces a wrong story when it drifts.
/// </summary>
public sealed class LogDictionaryTests
{
    [Fact]
    public void A_known_code_decodes_to_its_name()
    {
        Assert.Equal("auto_arm", LogDictionary.CodeName(56));
        Assert.Equal("state", LogDictionary.TagName(4));
        Assert.Equal("WARN", LogDictionary.LevelName(2));
    }

    [Fact]
    public void An_unknown_code_is_shown_rather_than_swallowed()
    {
        // A record from a newer firmware must still be readable as "something
        // happened, code 200" rather than vanishing or throwing.
        Assert.Equal("code200", LogDictionary.CodeName(200));
        Assert.Equal("tag99", LogDictionary.TagName(99));
        Assert.Equal("L9", LogDictionary.LevelName(9));
    }

    [Fact]
    public void A_state_argument_reads_as_the_state()
    {
        // "state_enter 2" is a lookup every single time; "state_enter ARMED" is
        // not. This is the whole reason DescribeArg exists.
        Assert.Equal("ARMED", LogDictionary.DescribeArg(50, (long)NodeState.Armed));
    }

    [Fact]
    public void An_accepted_command_unpacks_into_command_and_resulting_state()
    {
        // Low byte the command, next byte the state. One record rather than two,
        // because two can be split by a ring wrap.
        var packed = (long)CommandType.Arm | ((long)NodeState.Armed << 8);

        Assert.Equal("ARM -> ARMED", LogDictionary.DescribeArg(51, packed));
    }

    [Fact]
    public void A_rejected_command_unpacks_into_command_and_reason()
    {
        var packed = (long)CommandType.Fire | ((long)RejectReason.BadTransition << 8);

        Assert.Equal("FIRE refused (BadTransition)", LogDictionary.DescribeArg(52, packed));
    }

    [Fact]
    public void A_packed_value_this_build_does_not_know_still_renders()
    {
        var packed = 99L | (200L << 8);

        Assert.Equal("command99 -> state200", LogDictionary.DescribeArg(51, packed));
    }

    [Fact]
    public void A_clean_self_test_says_so_rather_than_showing_zero()
    {
        Assert.Equal("clean", LogDictionary.DescribeArg(12, 0));
        Assert.Equal("0x06", LogDictionary.DescribeArg(12, 6));
    }

    [Fact]
    public void A_battery_argument_is_rendered_as_volts()
    {
        // Sent in tenths of a volt; "39" on a page would be read as 39 volts.
        Assert.Equal("3.9 V", LogDictionary.DescribeArg(90, 39));
    }

    [Fact]
    public void A_countdown_argument_carries_its_unit()
    {
        Assert.Equal("300 s", LogDictionary.DescribeArg(53, 300));
        Assert.Equal("42 s left", LogDictionary.DescribeArg(55, 42));
    }

    [Fact]
    public void A_code_with_nothing_to_say_says_nothing()
    {
        Assert.Equal(string.Empty, LogDictionary.DescribeArg(0, 0));

        // An unremarkable code with a zero argument should not print a bare "0".
        Assert.Equal(string.Empty, LogDictionary.DescribeArg(1, 0));
    }

    [Fact]
    public void Every_tag_and_level_offered_as_a_filter_decodes()
    {
        // The filter drop-downs are built from these, so an entry that does not
        // round-trip would be an option that matches nothing.
        foreach (var tag in LogDictionary.AllTags)
            Assert.Equal(tag.Value, LogDictionary.TagName(tag.Key));

        foreach (var level in LogDictionary.AllLevels)
            Assert.Equal(level.Value, LogDictionary.LevelName(level.Key));
    }

    [Fact]
    public void A_record_at_warn_or_worse_is_noteworthy()
    {
        Assert.True(new LogRecord { DeviceSerial = "IN-1", Level = (byte)LogLevel.Error }.IsNoteworthy);
        Assert.True(new LogRecord { DeviceSerial = "IN-1", Level = (byte)LogLevel.Warn }.IsNoteworthy);
        Assert.False(new LogRecord { DeviceSerial = "IN-1", Level = (byte)LogLevel.Info }.IsNoteworthy);
    }
}
