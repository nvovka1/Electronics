using LoraFleet.Domain.Devices;
using LoraFleet.Domain.Logs;

namespace LoraFleet.Tests;

/// <summary>
/// The dictionary is a contract with the firmware's LogCode enum. If these
/// numbers drift, every stored dump decodes to the wrong story - silently,
/// which is the worst way for it to happen.
/// </summary>
public sealed class LogDictionaryTests
{
    [Theory]
    [InlineData(0, "PANIC")]
    [InlineData(2, "WARN")]
    [InlineData(5, "TRACE")]
    public void Levels_decode(byte level, string expected)
    {
        Assert.Equal(expected, LogDictionary.LevelName(level));
    }

    [Theory]
    [InlineData(1, "post")]
    [InlineData(3, "radio")]
    [InlineData(6, "batt")]
    [InlineData(7, "net")]
    [InlineData(8, "ota")]
    public void Tags_decode(byte tag, string expected)
    {
        Assert.Equal(expected, LogDictionary.TagName(tag));
    }

    [Theory]
    [InlineData(22, "cfg_migrated")]
    [InlineData(33, "tx_noack")]
    [InlineData(40, "lowbat_write_blocked")]
    [InlineData(63, "wifi_up")]
    [InlineData(89, "ota_trial")]
    [InlineData(91, "ota_rollback")]
    public void Codes_decode(byte code, string expected)
    {
        Assert.Equal(expected, LogDictionary.CodeName(code));
    }

    [Fact]
    public void An_unknown_code_says_so_rather_than_guessing()
    {
        // A newer firmware can send a code this build has never heard of. That
        // has to read as unknown, not as whichever code happens to be nearby.
        Assert.Equal("unknown(199)", LogDictionary.CodeName(199));
    }

    [Fact]
    public void Boot_renders_the_reset_reason()
    {
        Assert.Equal("POWERON", LogDictionary.DescribeArg(1, 1));
        Assert.Equal("TASK_WDT", LogDictionary.DescribeArg(1, 6));
    }

    [Fact]
    public void Cfg_loaded_unpacks_the_slot_and_sequence()
    {
        // The firmware packs (slot << 24) | seq. Showing that raw turns a
        // readable event into a nine-digit number.
        Assert.Equal("slot A seq 16", LogDictionary.DescribeArg(20, 16));
        Assert.Equal("slot B seq 17", LogDictionary.DescribeArg(20, (1L << 24) | 17));
    }

    [Fact]
    public void Cfg_slot_bad_carries_a_bare_slot_index()
    {
        // Unlike cfg_loaded, this one is not packed - it is just the index.
        Assert.Equal("slot A", LogDictionary.DescribeArg(26, 0));
        Assert.Equal("slot B", LogDictionary.DescribeArg(26, 1));
    }

    [Fact]
    public void Cfg_migrated_shows_both_versions()
    {
        Assert.Equal("v1 -> v2", LogDictionary.DescribeArg(22, (1L << 16) | 2));
    }

    [Fact]
    public void Rx_unpacks_source_and_sequence()
    {
        Assert.Equal("src 2 seq 41", LogDictionary.DescribeArg(32, (2L << 16) | 41));
    }

    [Fact]
    public void Post_mask_names_the_failing_blocks()
    {
        Assert.Equal("0x0008 (radio)", LogDictionary.DescribeArg(12, 0x0008));
        Assert.Equal("0x000C (adc, radio)", LogDictionary.DescribeArg(12, 0x000C));
        Assert.Equal("0x0000 (OK)", LogDictionary.DescribeArg(12, 0));
    }

    [Fact]
    public void Ack_rssi_is_read_as_signed()
    {
        // The firmware sign-extends an int8 into a uint32 argument. Read as
        // unsigned it would come back as 4294967199 instead of -97.
        Assert.Equal("-97 dBm", LogDictionary.DescribeArg(36, unchecked((uint)-97)));
    }

    [Fact]
    public void Millivolt_arguments_carry_their_unit()
    {
        Assert.Equal("3120 mV", LogDictionary.DescribeArg(40, 3120));
    }

    [Fact]
    public void Post_blocks_match_the_firmware_table()
    {
        Assert.Equal(6, PostBlock.All.Count);

        Assert.Equal("power", PostBlock.All[0].Name);
        Assert.Equal("radio", PostBlock.All[3].Name);

        // Only power and radio are critical: without them the node cannot do
        // its job. The rest degrade.
        Assert.True(PostBlock.HasCriticalFailure(0x0008));  // radio
        Assert.True(PostBlock.HasCriticalFailure(0x0001));  // power
        Assert.False(PostBlock.HasCriticalFailure(0x0004)); // adc
        Assert.False(PostBlock.HasCriticalFailure(0x0030)); // display + button
    }

    [Fact]
    public void A_clean_mask_describes_itself_as_ok()
    {
        Assert.Equal("OK", PostBlock.Describe(0));
        Assert.Empty(PostBlock.Failing(0));
    }

    [Fact]
    public void Wifi_up_renders_the_address_little_endian_first()
    {
        // The firmware casts an IPAddress straight to uint32, and the ESP32
        // stores it with the first octet in the low byte. Reading it the other
        // way round produces a plausible-looking address that is wrong.
        const long packed = 192 | (168L << 8) | (1L << 16) | (57L << 24);
        Assert.Equal("192.168.1.57", LogDictionary.DescribeArg(63, packed));
    }

    [Fact]
    public void A_negative_report_status_is_a_client_error_not_an_http_code()
    {
        // FLEET_ERR_NOT_READY. Rendered bare it is "-1000", which tells an
        // operator nothing about a node that has simply never been given a key.
        Assert.Equal(
            "no link, no credentials, or no clock for TLS",
            LogDictionary.DescribeArg(68, -1000));

        // A real HTTP status still reads as one.
        Assert.Equal("HTTP 401", LogDictionary.DescribeArg(68, 401));
    }

    [Fact]
    public void Ota_check_calls_204_out_as_nothing_to_do()
    {
        // The common answer, and the one an operator most often misreads as a
        // failure because it is not a 200.
        Assert.Equal("204 nothing to do", LogDictionary.DescribeArg(80, 204));
    }

    [Fact]
    public void Ota_refusal_names_the_gate_that_said_no()
    {
        Assert.Equal("battery below ota_vbat_min_mv", LogDictionary.DescribeArg(82, 3));
        Assert.Equal(
            "the image is built for a different board", LogDictionary.DescribeArg(82, 5));
        Assert.Equal(
            "this version already failed its trial on this node",
            LogDictionary.DescribeArg(82, 9));
    }

    [Fact]
    public void Ota_rollback_says_why_rather_than_giving_a_number()
    {
        // This is the single most important line in any dump from a node that
        // reverted: it distinguishes "the new image was broken" from "nobody
        // could reach it" from "somebody asked".
        Assert.Equal(
            "a critical POST block failed on the new image",
            LogDictionary.DescribeArg(91, 2));
        Assert.Equal(
            "never checked in during the trial window", LogDictionary.DescribeArg(91, 1));
    }

    [Fact]
    public void Net_cfg_changed_names_the_field_and_never_the_value()
    {
        // Two of the four are secrets, so the firmware logs only the index.
        Assert.Equal("api key", LogDictionary.DescribeArg(61, 3));
    }

    [Fact]
    public void Time_synced_is_the_one_record_carrying_real_wall_clock_time()
    {
        Assert.Equal("2026-09-06 12:00:00Z", LogDictionary.DescribeArg(66, 1788696000));
    }
}

