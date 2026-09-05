using System.Diagnostics;
using LoraFleet.Web.Models;
using Microsoft.AspNetCore.Mvc;

namespace LoraFleet.Web.Controllers;

/// <summary>
/// Only the error page. The fleet list is the front page, and it lives on
/// DevicesController where it belongs.
/// </summary>
public sealed class HomeController : Controller
{
    [ResponseCache(Duration = 0, Location = ResponseCacheLocation.None, NoStore = true)]
    public IActionResult Error()
    {
        return View(new ErrorViewModel
        {
            RequestId = Activity.Current?.Id ?? HttpContext.TraceIdentifier,
        });
    }
}
