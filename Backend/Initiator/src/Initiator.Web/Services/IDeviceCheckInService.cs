using Initiator.Domain.Devices;
using Initiator.Web.Contracts;

namespace Initiator.Web.Services;

public interface IDeviceCheckInService
{
    /// <summary>
    /// Records a node's periodic report, enrolling it if this is the first one.
    /// </summary>
    Task<Device> RecordCheckInAsync(
        string serial, CheckInRequest request, CancellationToken cancellationToken = default);
}
