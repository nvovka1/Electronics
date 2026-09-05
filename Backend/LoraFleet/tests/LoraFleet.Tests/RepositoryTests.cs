using LoraFleet.DataAccess.Repositories;
using LoraFleet.Domain.Devices;
using LoraFleet.Domain.Firmware;
using LoraFleet.Domain.Logs;

namespace LoraFleet.Tests;

[Collection(MongoCollection.Name)]
public sealed class RepositoryTests
{
    private readonly MongoFixture _mongo;

    public RepositoryTests(MongoFixture mongo) => _mongo = mongo;

    /// <summary>A database per test, so nothing depends on the order they run in.</summary>
    private (MongoDeviceRepository Devices, MongoLogRepository Logs, MongoFirmwareRepository Firmware)
        Repositories([System.Runtime.CompilerServices.CallerMemberName] string name = "")
    {
        var context = _mongo.CreateContext($"test_{name.ToLowerInvariant()}");
        return (new MongoDeviceRepository(context),
                new MongoLogRepository(context),
                new MongoFirmwareRepository(context));
    }

    private static Device NewDevice(string serial = "LQ-A41C", string version = "1.0.0") => new()
    {
        Serial = serial,
        NodeId = 1,
        HardwareId = "ttgo-lora32-v21new",
        FirmwareVersion = version,
        FirmwareHash = "59b0d70",
        BuildType = "field",
        ProtocolVersion = 1,
        ConfigVersion = 2,
        EnrolledAt = DateTime.UtcNow,
        LastSeenAt = DateTime.UtcNow,
        LastHealth = new DeviceHealth
        {
            ReceivedAt = DateTime.UtcNow,
            UptimeSeconds = 1234,
            Reboots = 3,
            LastCrashCode = 1,
            LastCrashReason = "POWERON",
            BatteryDeciVolts = 39,
            LastRssi = -97,
            PostMask = 0,
        },
    };

    [Fact]
    public async Task A_device_enrols_itself_on_its_first_check_in()
    {
        var (devices, _, _) = Repositories();

        var stored = await devices.UpsertCheckInAsync(NewDevice());

        Assert.Equal("LQ-A41C", stored.Serial);
        Assert.Equal(1, stored.NodeId);
        Assert.NotNull(stored.LastHealth);
        Assert.Equal(39, stored.LastHealth!.BatteryDeciVolts);
        Assert.Equal("POWERON", stored.LastHealth.LastCrashReason);

        Assert.Single(await devices.GetAllAsync());
    }

    [Fact]
    public async Task The_serial_is_the_key_so_repeat_check_ins_do_not_duplicate()
    {
        var (devices, _, _) = Repositories();

        await devices.UpsertCheckInAsync(NewDevice());
        await devices.UpsertCheckInAsync(NewDevice(version: "1.1.0"));

        var all = await devices.GetAllAsync();
        Assert.Single(all);
        Assert.Equal("1.1.0", all[0].FirmwareVersion);
    }

    [Fact]
    public async Task A_check_in_never_overwrites_what_the_operator_set()
    {
        // This is the one that would be silently wrong with a naive replace:
        // a node reporting in would wipe its own target version and notes.
        var (devices, _, _) = Repositories();

        await devices.UpsertCheckInAsync(NewDevice());
        await devices.SetTargetFirmwareAsync("LQ-A41C", "1.2.0");
        await devices.SetNotesAsync("LQ-A41C", "north mast, ladder needed");
        await devices.SetDecommissionedAsync("LQ-A41C", true);

        await devices.UpsertCheckInAsync(NewDevice(version: "1.1.0"));

        var stored = await devices.GetBySerialAsync("LQ-A41C");
        Assert.NotNull(stored);
        Assert.Equal("1.1.0", stored!.FirmwareVersion);          // the device's own field moved
        Assert.Equal("1.2.0", stored.TargetFirmwareVersion);      // the operator's did not
        Assert.Equal("north mast, ladder needed", stored.Notes);
        Assert.True(stored.IsDecommissioned);
    }

    [Fact]
    public async Task The_enrolment_date_is_the_first_check_in_not_the_latest()
    {
        var (devices, _, _) = Repositories();

        var first = NewDevice();
        first.LastSeenAt = new DateTime(2026, 1, 1, 0, 0, 0, DateTimeKind.Utc);
        await devices.UpsertCheckInAsync(first);

        var later = NewDevice();
        later.LastSeenAt = new DateTime(2026, 6, 1, 0, 0, 0, DateTimeKind.Utc);
        await devices.UpsertCheckInAsync(later);

        var stored = await devices.GetBySerialAsync("LQ-A41C");
        Assert.Equal(2026, stored!.EnrolledAt.Year);
        Assert.Equal(1, stored.EnrolledAt.Month);
        Assert.Equal(6, stored.LastSeenAt.Month);
    }

    [Fact]
    public async Task Log_records_round_trip_and_decode()
    {
        var (_, logs, _) = Repositories();

        await logs.AppendAsync(
        [
            new LogRecord
            {
                DeviceSerial = "LQ-A41C",
                TimestampMs = 41,
                ReceivedAt = DateTime.UtcNow,
                Level = 2, Tag = 2, Code = 26, Arg = 1,
            },
        ]);

        var stored = await logs.QueryAsync(new LogQuery { DeviceSerial = "LQ-A41C" });

        var record = Assert.Single(stored);
        Assert.Equal("WARN", record.LevelName);
        Assert.Equal("cfg", record.TagName);
        Assert.Equal("cfg_slot_bad", record.CodeName);
        Assert.Equal("slot B", record.ArgDescription);
        Assert.False(string.IsNullOrEmpty(record.Id));
    }

    [Fact]
    public async Task Severity_filtering_means_this_level_and_worse()
    {
        var (_, logs, _) = Repositories();
        var now = DateTime.UtcNow;

        await logs.AppendAsync(
        [
            new LogRecord { DeviceSerial = "A", ReceivedAt = now, Level = 1, Code = 10 }, // ERROR
            new LogRecord { DeviceSerial = "A", ReceivedAt = now, Level = 2, Code = 33 }, // WARN
            new LogRecord { DeviceSerial = "A", ReceivedAt = now, Level = 3, Code = 31 }, // INFO
            new LogRecord { DeviceSerial = "B", ReceivedAt = now, Level = 1, Code = 10 },
        ]);

        var warnAndAbove = await logs.QueryAsync(new LogQuery { MaxLevel = 2 });
        Assert.Equal(3, warnAndAbove.Count);
        Assert.All(warnAndAbove, record => Assert.True(record.Level <= 2));

        var oneDevice = await logs.QueryAsync(new LogQuery { DeviceSerial = "A", MaxLevel = 2 });
        Assert.Equal(2, oneDevice.Count);
    }

    [Fact]
    public async Task Deleting_a_device_takes_its_log_with_it()
    {
        var (devices, logs, _) = Repositories();

        await devices.UpsertCheckInAsync(NewDevice());
        await logs.AppendAsync(
        [
            new LogRecord { DeviceSerial = "LQ-A41C", ReceivedAt = DateTime.UtcNow, Level = 3, Code = 1 },
        ]);

        await logs.DeleteForDeviceAsync("LQ-A41C");
        await devices.DeleteAsync("LQ-A41C");

        Assert.Empty(await devices.GetAllAsync());
        Assert.Equal(0, await logs.CountAsync(new LogQuery { DeviceSerial = "LQ-A41C" }));
    }

    [Fact]
    public async Task Firmware_binaries_round_trip_through_gridfs()
    {
        var (_, _, firmware) = Repositories();

        var payload = new byte[] { 0xE9, 0x06, 0x02, 0x20, 0xAA, 0xBB };
        using var upload = new MemoryStream(payload);
        var fileId = await firmware.StoreBinaryAsync("test.bin", upload);

        await firmware.UpsertAsync(new FirmwareRelease
        {
            Id = FirmwareRelease.BuildId("1.0.0", "59b0d70", "field"),
            Version = "1.0.0",
            GitHash = "59b0d70",
            BuildType = "field",
            HardwareId = "ttgo-lora32-v21new",
            SizeBytes = payload.Length,
            Sha256 = "abc",
            BuildUtc = DateTime.UtcNow,
            UploadedAt = DateTime.UtcNow,
            BinaryFileId = fileId,
        });

        var stored = await firmware.GetByIdAsync("1.0.0+59b0d70-field");
        Assert.NotNull(stored);
        Assert.True(stored!.IsDeployable);

        await using var download = await firmware.OpenBinaryAsync(fileId);
        Assert.NotNull(download);
        using var buffer = new MemoryStream();
        await download!.CopyToAsync(buffer);
        Assert.Equal(payload, buffer.ToArray());
    }

    [Fact]
    public async Task A_dirty_release_is_never_offered_as_the_latest()
    {
        var (_, _, firmware) = Repositories();

        await firmware.UpsertAsync(new FirmwareRelease
        {
            Id = "1.1.0+aaaaaaa-field",
            Version = "1.1.0",
            BuildType = "field",
            HardwareId = "ttgo-lora32-v21new",
            IsDirty = true,
            BinaryFileId = "000000000000000000000000",
            BuildUtc = DateTime.UtcNow,
        });

        await firmware.UpsertAsync(new FirmwareRelease
        {
            Id = "1.0.0+59b0d70-field",
            Version = "1.0.0",
            BuildType = "field",
            HardwareId = "ttgo-lora32-v21new",
            BinaryFileId = "000000000000000000000000",
            BuildUtc = DateTime.UtcNow.AddDays(-1),
        });

        var latest = await firmware.GetLatestForHardwareAsync("ttgo-lora32-v21new", "field");

        // The dirty one is newer, and must still lose: there is no commit to
        // reproduce it from.
        Assert.NotNull(latest);
        Assert.Equal("1.0.0", latest!.Version);
    }

    [Fact]
    public async Task A_release_for_a_different_board_is_not_offered()
    {
        var (_, _, firmware) = Repositories();

        await firmware.UpsertAsync(new FirmwareRelease
        {
            Id = "1.0.0+59b0d70-field",
            Version = "1.0.0",
            BuildType = "field",
            HardwareId = "some-other-board",
            BinaryFileId = "000000000000000000000000",
            BuildUtc = DateTime.UtcNow,
        });

        Assert.Null(await firmware.GetLatestForHardwareAsync("ttgo-lora32-v21new", "field"));
    }

    [Fact]
    public async Task Deleting_a_release_removes_its_image_too()
    {
        var (_, _, firmware) = Repositories();

        using var upload = new MemoryStream([1, 2, 3, 4]);
        var fileId = await firmware.StoreBinaryAsync("test.bin", upload);

        await firmware.UpsertAsync(new FirmwareRelease
        {
            Id = "1.0.0+abc-field",
            Version = "1.0.0",
            BuildType = "field",
            BinaryFileId = fileId,
            BuildUtc = DateTime.UtcNow,
        });

        await firmware.DeleteAsync("1.0.0+abc-field");

        Assert.Null(await firmware.GetByIdAsync("1.0.0+abc-field"));
        // An orphaned GridFS file is invisible and never cleaned up, so the
        // image must go with the record.
        Assert.Null(await firmware.OpenBinaryAsync(fileId));
    }
}
