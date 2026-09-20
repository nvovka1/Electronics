using System.Text;
using Initiator.DataAccess.Repositories;
using Initiator.Web.Infrastructure;
using Initiator.Web.Models;
using Microsoft.AspNetCore.Mvc;

namespace Initiator.Web.Controllers;

/// <summary>
/// The flights the aircraft have sent up, and the CSV download.
/// </summary>
[Route("flights")]
public sealed class FlightsController : Controller
{
    /// <summary>Rows shown on the detail page. The download is the way to get all of them.</summary>
    private const int PreviewRows = 200;

    private readonly IFlightRepository _flights;
    private readonly ITelemetrySampleRepository _samples;

    public FlightsController(IFlightRepository flights, ITelemetrySampleRepository samples)
    {
        _flights = flights ?? throw new ArgumentNullException(nameof(flights));
        _samples = samples ?? throw new ArgumentNullException(nameof(samples));
    }

    [HttpGet("")]
    public async Task<IActionResult> Index(string? serial, CancellationToken cancellationToken)
    {
        var model = new FlightListViewModel
        {
            Serial = serial,
            Aircraft = await _flights.ListAircraftAsync(cancellationToken),
            Flights = await _flights.ListAsync(serial, 200, cancellationToken),
        };

        ViewData["Title"] = "Flights";
        return View(model);
    }

    [HttpGet("{serial}/{flightId:int}")]
    public async Task<IActionResult> Details(
        string serial, int flightId, CancellationToken cancellationToken)
    {
        var flight = await _flights.GetAsync(serial, flightId, cancellationToken);
        if (flight is null) return NotFound();

        var model = new FlightDetailsViewModel
        {
            Flight = flight,
            Samples = await _samples.PageAsync(serial, flightId, 0, PreviewRows, cancellationToken),
            PreviewRows = PreviewRows,
        };

        ViewData["Title"] = $"Flight {flightId} · {serial}";
        return View(model);
    }

    /// <summary>
    /// The download. Streamed row by row: a flight is tens of thousands of rows
    /// and building the file in memory first would make the largest flight the
    /// one that cannot be exported.
    /// </summary>
    [HttpGet("{serial}/{flightId:int}/csv")]
    public async Task<IActionResult> Csv(
        string serial, int flightId, CancellationToken cancellationToken)
    {
        var flight = await _flights.GetAsync(serial, flightId, cancellationToken);
        if (flight is null) return NotFound();

        Response.ContentType = "text/csv; charset=utf-8";
        Response.Headers.ContentDisposition =
            $"attachment; filename=\"{serial}-flight-{flightId}.csv\"";

        await using var writer = new StreamWriter(Response.Body, Encoding.UTF8);

        await writer.WriteLineAsync(TelemetryCsv.Header);

        await foreach (var sample in _samples.StreamAsync(serial, flightId, cancellationToken))
        {
            await writer.WriteLineAsync(TelemetryCsv.Row(sample));
        }

        await writer.FlushAsync(cancellationToken);
        return new EmptyResult();
    }

    [HttpPost("{serial}/{flightId:int}/delete")]
    [ValidateAntiForgeryToken]
    public async Task<IActionResult> Delete(
        string serial, int flightId, CancellationToken cancellationToken)
    {
        // Rows first: a flight document removed while its rows survive would
        // leave them unreachable and uncountable.
        var rows = await _samples.DeleteForFlightAsync(serial, flightId, cancellationToken);
        var removed = await _flights.DeleteAsync(serial, flightId, cancellationToken);

        if (removed)
        {
            TempData["Message"] =
                $"Flight {flightId} of {serial} deleted here, with {rows} rows. " +
                "The board may still have its own copy.";
        }
        else
        {
            TempData["Error"] = $"Flight {flightId} of {serial} was already gone.";
        }

        return RedirectToAction(nameof(Index));
    }
}
