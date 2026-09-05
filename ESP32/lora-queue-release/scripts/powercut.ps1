<#
.SYNOPSIS
    Drives the power-cut-during-config-write experiment.

.DESCRIPTION
    Automates everything except the cut itself: reads the config before the
    write, issues the write, prompts for the power cut, waits for the board to
    come back and reads the config again. Writes a CSV of the results.

    See docs/POWER_CUT_EXPERIMENT.md for what each outcome means.

    The board must be on USB power with the battery DISCONNECTED. With a
    battery fitted, "power off" is a lie and the experiment measures nothing.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\powercut.ps1 -Port COM5 -Cycles 20
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$Cycles = 20,
    [int]$BaudRate = 115200,
    [string]$OutFile = 'powercut-results.csv'
)

$ErrorActionPreference = 'Stop'

# Alternating so the value genuinely changes on every cycle; writing the same
# number twice would prove nothing.
$values = @(17, 14)

function Open-Board {
    param([string]$PortName, [int]$Baud)

    $sp = New-Object System.IO.Ports.SerialPort $PortName, $Baud, 'None', 8, 'One'
    $sp.ReadTimeout = 3000
    $sp.NewLine = "`n"
    # The LoRa32 resets when DTR/RTS assert; that is wanted on open so the
    # session always starts from a known boot.
    $sp.DtrEnable = $true
    $sp.RtsEnable = $true
    $sp.Open()
    return $sp
}

function Read-Until-Quiet {
    param($Sp, [int]$QuietMs = 400, [int]$MaxMs = 6000)

    $sb = New-Object System.Text.StringBuilder
    $lastData = [Environment]::TickCount
    $deadline = [Environment]::TickCount + $MaxMs

    while ([Environment]::TickCount -lt $deadline) {
        if ($Sp.BytesToRead -gt 0) {
            [void]$sb.Append($Sp.ReadExisting())
            $lastData = [Environment]::TickCount
        } elseif (([Environment]::TickCount - $lastData) -gt $QuietMs) {
            break
        } else {
            Start-Sleep -Milliseconds 25
        }
    }
    return $sb.ToString()
}

function Invoke-Command-OnBoard {
    param($Sp, [string]$Line)

    $Sp.DiscardInBuffer()
    $Sp.WriteLine($Line)
    return Read-Until-Quiet -Sp $Sp
}

function Get-ConfigState {
    param($Sp)

    $text = Invoke-Command-OnBoard -Sp $Sp -Line 'config get'

    $txPower = $null
    $seq = $null
    $slot = $null

    foreach ($line in $text -split "`r?`n") {
        if ($line -match '^\s*tx_power\s+(\d+)') { $txPower = [int]$Matches[1] }
        if ($line -match '^\s*cfg_seq\s+(\d+)\s+slot\s+(\S+)') {
            $seq = [int]$Matches[1]
            $slot = $Matches[2]
        }
    }

    return [pscustomobject]@{
        TxPower = $txPower
        Seq     = $seq
        Slot    = $slot
        Raw     = $text
    }
}

# --- run ------------------------------------------------------------------

Write-Host "Power-cut experiment on $Port, $Cycles cycles."
Write-Host 'Battery MUST be disconnected. Board on USB power only.'
Write-Host ''

$results = @()

for ($i = 1; $i -le $Cycles; $i++) {
    $target = $values[($i - 1) % $values.Count]

    Write-Host "--- cycle $i of $Cycles ---" -ForegroundColor Cyan

    $sp = Open-Board -PortName $Port -BaudRate $BaudRate
    Start-Sleep -Milliseconds 2000   # let it boot past the splash
    [void](Read-Until-Quiet -Sp $sp)

    $before = Get-ConfigState -Sp $sp
    Write-Host ("  before: tx_power={0} seq={1} slot={2}" -f `
            $before.TxPower, $before.Seq, $before.Slot)

    if ($before.TxPower -eq $target) {
        # Nothing would change, so nothing would be written. Flip to the other
        # value rather than run a cycle that tests nothing.
        $target = $values[$i % $values.Count]
    }

    Write-Host "  writing tx_power=$target -- CUT THE POWER NOW" -ForegroundColor Yellow
    $sp.DiscardInBuffer()
    $sp.WriteLine("config set tx_power $target")

    # Deliberately not timed by the script: a human flipping a switch lands at
    # a different point in the erase/write/commit sequence every time, which is
    # exactly the coverage this experiment needs.
    Write-Host '  press Enter once the power has been cut and restored'
    [void][Console]::ReadLine()

    try { $sp.Close() } catch {}
    Start-Sleep -Milliseconds 500

    $sp = Open-Board -PortName $Port -BaudRate $BaudRate
    Start-Sleep -Milliseconds 2500
    [void](Read-Until-Quiet -Sp $sp)

    $after = Get-ConfigState -Sp $sp
    $log = Invoke-Command-OnBoard -Sp $sp -Line 'log dump 20'
    $slotBad = $log -match 'cfg_slot_bad'
    $onDefaults = $log -match 'cfg_defaults'

    $verdict = 'UNKNOWN'
    if ($onDefaults) {
        $verdict = 'FAIL - came up on defaults'
    } elseif ($after.TxPower -eq $before.TxPower -and $after.Seq -eq $before.Seq) {
        $verdict = 'PASS - old value kept'
    } elseif ($after.TxPower -eq $target -and $after.Seq -eq ($before.Seq + 1)) {
        $verdict = 'PASS - new value written'
    } else {
        $verdict = 'FAIL - unexpected value or sequence'
    }

    $colour = if ($verdict -like 'PASS*') { 'Green' } else { 'Red' }
    Write-Host ("  after:  tx_power={0} seq={1} slot={2}  slot_bad={3}" -f `
            $after.TxPower, $after.Seq, $after.Slot, $slotBad)
    Write-Host "  $verdict" -ForegroundColor $colour

    $results += [pscustomobject]@{
        Cycle        = $i
        TargetValue  = $target
        BeforeValue  = $before.TxPower
        BeforeSeq    = $before.Seq
        BeforeSlot   = $before.Slot
        AfterValue   = $after.TxPower
        AfterSeq     = $after.Seq
        AfterSlot    = $after.Slot
        SlotRejected = [bool]$slotBad
        OnDefaults   = [bool]$onDefaults
        Verdict      = $verdict
    }

    try { $sp.Close() } catch {}
}

$results | Export-Csv -Path $OutFile -NoTypeInformation -Encoding UTF8

$passed = ($results | Where-Object { $_.Verdict -like 'PASS*' }).Count
$caught = ($results | Where-Object { $_.SlotRejected }).Count

Write-Host ''
Write-Host "results written to $OutFile"
Write-Host "$passed of $Cycles cycles passed"
Write-Host "$caught cycles caught a torn slot (cfg_slot_bad in the log)"

if ($caught -eq 0) {
    Write-Host ''
    Write-Host 'No torn slot was ever seen. The cuts did not land inside a write,' -ForegroundColor Yellow
    Write-Host 'so the mechanism has not actually been exercised. Cut sooner.' -ForegroundColor Yellow
}

exit ($(if ($passed -eq $Cycles) { 0 } else { 1 }))
