param(
    [string]$ServerAddress = "127.0.0.1",
    [ValidateRange(1, 65535)]
    [int]$ServerPort = 9000,
    [ValidateRange(1, 120)]
    [int]$TimeoutSeconds = 20
)

$ErrorActionPreference = "Stop"
$client = [System.Net.Sockets.TcpClient]::new()
$script:heartbeatSequence = 1
$script:lastHeartbeatAt = [System.Diagnostics.Stopwatch]::StartNew()

function Send-ProtocolMessage {
    param([hashtable]$Message)
    $json = $Message | ConvertTo-Json -Compress
    Write-Host "SEND $json"
    $script:writer.WriteLine($json)
}

function Send-HeartbeatIfDue {
    if ($script:lastHeartbeatAt.ElapsedMilliseconds -ge 750) {
        Send-ProtocolMessage @{
            version = 1
            type = "heartbeat"
            sequence = $script:heartbeatSequence
        }
        $script:heartbeatSequence++
        $script:lastHeartbeatAt.Restart()
    }
}

function Read-ProtocolMessage {
    param([datetime]$Deadline)
    while ([datetime]::UtcNow -lt $Deadline) {
        Send-HeartbeatIfDue
        if ($script:stream.DataAvailable) {
            $line = $script:reader.ReadLine()
            if ($null -eq $line) {
                throw "The server closed the connection."
            }
            Write-Host "RECV $line"
            try {
                $message = $line | ConvertFrom-Json
            }
            catch {
                throw "The server returned invalid JSON: $line"
            }
            if ($message.version -ne 1) {
                throw "Unexpected protocol version in: $line"
            }
            if ($message.type -eq "protocolError") {
                throw "Server protocol error: $line"
            }
            if ($message.type -eq "heartbeatAck") {
                continue
            }
            return $message
        }
        Start-Sleep -Milliseconds 25
    }
    throw "Timed out waiting for a protocol message."
}

function Wait-ForType {
    param(
        [string]$Type,
        [string]$TaskId = ""
    )
    $message = Read-ProtocolMessage ([datetime]::UtcNow.AddSeconds($TimeoutSeconds))
    if ($message.type -ne $Type) {
        throw "Expected $Type but received $($message.type)."
    }
    if ($TaskId -and $message.taskId -ne $TaskId) {
        throw "Expected taskId $TaskId but received $($message.taskId)."
    }
    return $message
}

function Acknowledge-Result {
    param([string]$TaskId)
    Send-ProtocolMessage @{
        version = 1
        type = "resultAck"
        taskId = $TaskId
    }
}

function Invoke-Locate {
    param(
        [string]$TaskId,
        [ValidateSet("EDGE", "MARK_POINT")]
        [string]$Mode
    )
    Send-ProtocolMessage @{
        version = 1
        type = "prepareLocate"
        taskId = $TaskId
        mode = $Mode
    }
    $prepared = Wait-ForType "locatePrepared" $TaskId
    if ($prepared.mode -ne $Mode) {
        throw "Prepared mode mismatch for task $TaskId."
    }
    $result = Wait-ForType "locateResult" $TaskId
    if (@("OK", "NG", "ERROR") -notcontains $result.outcome) {
        throw "Unknown locate outcome for task $TaskId."
    }
    if ($result.outcome -eq "NG" -and
        ($result.x -ne 0 -or $result.y -ne 0 -or $result.r -ne 0)) {
        throw "NG result did not contain zero coordinates."
    }
    Acknowledge-Result $TaskId
    return $result
}

function Invoke-Recover {
    param([int]$Sequence)
    Send-ProtocolMessage @{
        version = 1
        type = "recover"
        sequence = $Sequence
    }
    $reply = Wait-ForType "recoverResult"
    if ($reply.sequence -ne $Sequence -or $reply.outcome -ne "OK") {
        throw "Recovery failed or returned a mismatched sequence."
    }
}

try {
    $client.Connect($ServerAddress, $ServerPort)
    $script:stream = $client.GetStream()
    $script:stream.ReadTimeout = $TimeoutSeconds * 1000
    $script:writer = [System.IO.StreamWriter]::new(
        $script:stream,
        [System.Text.UTF8Encoding]::new($false), 1024, $true)
    $script:reader = [System.IO.StreamReader]::new(
        $script:stream,
        [System.Text.UTF8Encoding]::new($false), $false, 1024, $true)
    $script:writer.AutoFlush = $true
    $script:writer.NewLine = "`n"

    Send-ProtocolMessage @{ version = 1; type = "queryStatus"; sequence = 1 }
    $initialStatus = Wait-ForType "statusReply"
    if ($initialStatus.sequence -ne 1) {
        throw "Initial status sequence mismatch."
    }

    $firstResult = Invoke-Locate "1001" "EDGE"
    if ($firstResult.outcome -eq "ERROR") {
        Write-Host "The configured first-capture failure was observed; recovering."
        Invoke-Recover 2
    }

    Send-ProtocolMessage @{
        version = 1; type = "prepareLocate"; taskId = "1002"; mode = "EDGE"
    }
    [void](Wait-ForType "locatePrepared" "1002")
    Send-ProtocolMessage @{
        version = 1; type = "prepareLocate"; taskId = "1003"; mode = "MARK_POINT"
    }
    Send-ProtocolMessage @{ version = 1; type = "queryStatus"; sequence = 3 }

    $gotBusy = $false
    $gotBusyStatus = $false
    $busyResult = $null
    $deadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (!$gotBusy -or !$gotBusyStatus -or $null -eq $busyResult) {
        $message = Read-ProtocolMessage $deadline
        switch ($message.type) {
            "prepareRejected" {
                if ($message.taskId -ne "1003" -or $message.errorCode -ne "BUSY") {
                    throw "Unexpected prepareRejected response."
                }
                $gotBusy = $true
            }
            "statusReply" {
                if ($message.sequence -ne 3 -or $message.taskState -eq "IDLE") {
                    throw "QueryStatus did not report the in-flight task."
                }
                $gotBusyStatus = $true
            }
            "locateResult" {
                if ($message.taskId -ne "1002") {
                    throw "Unexpected locate result during Busy exercise."
                }
                $busyResult = $message
                Acknowledge-Result "1002"
            }
            default { throw "Unexpected response during Busy exercise: $($message.type)" }
        }
    }
    if ($busyResult.outcome -ne "OK") {
        throw "The post-recovery Edge simulation did not return OK."
    }

    $markResult = Invoke-Locate "1004" "MARK_POINT"
    if ($markResult.outcome -ne "NG") {
        throw "The configured MarkPoint simulation did not return NG."
    }

    Write-Host "Simulation TCP flow passed: status, failure/recovery, Busy, Edge OK, MarkPoint NG, heartbeat, and ACK."
}
finally {
    if ($null -ne $script:reader) { $script:reader.Dispose() }
    if ($null -ne $script:writer) { $script:writer.Dispose() }
    if ($null -ne $script:stream) { $script:stream.Dispose() }
    $client.Dispose()
}
