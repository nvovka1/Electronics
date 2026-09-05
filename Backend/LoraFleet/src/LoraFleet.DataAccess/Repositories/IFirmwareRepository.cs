using LoraFleet.Domain.Firmware;

namespace LoraFleet.DataAccess.Repositories;

public interface IFirmwareRepository
{
    Task<IReadOnlyList<FirmwareRelease>> GetAllAsync(CancellationToken cancellationToken = default);

    Task<FirmwareRelease?> GetByIdAsync(string id, CancellationToken cancellationToken = default);

    /// <summary>
    /// The newest deployable release matching a board variant. Hardware id is
    /// part of the question, not a detail: an image built for a neighbouring
    /// revision has different pin assignments and will not merely misbehave.
    /// </summary>
    Task<FirmwareRelease?> GetLatestForHardwareAsync(
        string hardwareId, string buildType, CancellationToken cancellationToken = default);

    Task UpsertAsync(FirmwareRelease release, CancellationToken cancellationToken = default);

    /// <summary>Stores the image itself and returns its GridFS id.</summary>
    Task<string> StoreBinaryAsync(
        string fileName, Stream content, CancellationToken cancellationToken = default);

    Task<Stream?> OpenBinaryAsync(string fileId, CancellationToken cancellationToken = default);

    Task DeleteAsync(string id, CancellationToken cancellationToken = default);
}
