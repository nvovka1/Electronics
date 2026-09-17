using System.Diagnostics;
using Initiator.Web.Models;
using Microsoft.AspNetCore.Mvc;

namespace Initiator.Web.Controllers;

public sealed class HomeController : Controller
{
    /// <summary>The devices list is the front page; there is nothing a separate home screen would add.</summary>
    public IActionResult Index() => RedirectToAction("Index", "Devices");

    [ResponseCache(Duration = 0, Location = ResponseCacheLocation.None, NoStore = true)]
    public IActionResult Error() =>
        View(new ErrorViewModel
        {
            RequestId = Activity.Current?.Id ?? HttpContext.TraceIdentifier,
        });
}
