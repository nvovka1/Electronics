using LoraFleet.Domain.Firmware;
using MongoDB.Bson;
using MongoDB.Driver;

namespace LoraFleet.DataAccess.Repositories;

public sealed class MongoFirmwareRepository : IFirmwareRepository
{
    private readonly FleetDbContext _context;

    public MongoFirmwareRepository(FleetDbContext context)
    {
        _context = context ?? throw new ArgumentNullException(nameof(context));
    }

    public async Task<IReadOnlyList<FirmwareRelease>> GetAllAsync(CancellationToken cancellationToken = default)
    {
        return await _context.FirmwareReleases
            .Find(FilterDefinition<FirmwareRelease>.Empty)
            .SortByDescending(release => release.BuildUtc)
            .ToListAsync(cancellationToken);
    }

    public async Task<FirmwareRelease?> GetByIdAsync(string id, CancellationToken cancellationToken = default)
    {
        return await _context.FirmwareReleases
            .Find(release => release.Id == id)
            .FirstOrDefaultAsync(cancellationToken);
    }

    public async Task<FirmwareRelease?> GetLatestForHardwareAsync(
        string hardwareId, string buildType, CancellationToken cancellationToken = default)
    {
        // Dirty images are excluded here rather than filtered by the caller.
        // There is no commit to reproduce one from, so it can never be the
        // right answer to "what should this node be running".
        return await _context.FirmwareReleases
            .Find(release =>
                release.HardwareId == hardwareId &&
                release.BuildType == buildType &&
                !release.IsDirty &&
                release.BinaryFileId != null)
            .SortByDescending(release => release.BuildUtc)
            .FirstOrDefaultAsync(cancellationToken);
    }

    public Task UpsertAsync(FirmwareRelease release, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(release);

        return _context.FirmwareReleases.ReplaceOneAsync(
            existing => existing.Id == release.Id,
            release,
            new ReplaceOptions { IsUpsert = true },
            cancellationToken);
    }

    public async Task<string> StoreBinaryAsync(
        string fileName, Stream content, CancellationToken cancellationToken = default)
    {
        var id = await _context.FirmwareBinaries.UploadFromStreamAsync(
            fileName, content, cancellationToken: cancellationToken);
        return id.ToString();
    }

    public async Task<Stream?> OpenBinaryAsync(string fileId, CancellationToken cancellationToken = default)
    {
        if (!ObjectId.TryParse(fileId, out var id)) return null;

        try
        {
            return await _context.FirmwareBinaries.OpenDownloadStreamAsync(
                id, cancellationToken: cancellationToken);
        }
        catch (MongoDB.Driver.GridFS.GridFSFileNotFoundException)
        {
            // The release record outlived its binary. Callers turn this into a
            // 404 rather than a 500: it is a missing file, not a broken server.
            return null;
        }
    }

    public async Task DeleteAsync(string id, CancellationToken cancellationToken = default)
    {
        var release = await GetByIdAsync(id, cancellationToken);
        if (release is null) return;

        // Binary first: a release record with no file is recoverable, an
        // orphaned file in GridFS is invisible and never cleaned up.
        if (release.BinaryFileId is not null && ObjectId.TryParse(release.BinaryFileId, out var fileId))
        {
            try
            {
                await _context.FirmwareBinaries.DeleteAsync(fileId, cancellationToken);
            }
            catch (MongoDB.Driver.GridFS.GridFSFileNotFoundException)
            {
                // Already gone; deleting the record is still the right outcome.
            }
        }

        await _context.FirmwareReleases.DeleteOneAsync(release => release.Id == id, cancellationToken);
    }
}
