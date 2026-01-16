#!/bin/bash

# Check that jq is installed and return a nice message.
if ! command -v jq > /dev/null 2>&1; then
  echo "error: otel_collector_log support requires 'jq' to be installed" >&2
  exit 1
fi

# Validate input argument
if [[ -z "$2" ]]; then
  echo "error: missing input file argument" >&2
  exit 1
fi

# Validate input file exists
if [[ ! -f "$2" ]]; then
  echo "error: file not found: $2" >&2
  exit 1
fi

# We want jq output to come in UTC
export TZ=UTC

# Convert OTEL Collector File Exporter JSON to one-record-per-line format
# Input: JSON Lines where each line contains batched log records under resourceLogs
# Output: JSON Lines with one log record per line, flattened with resource attributes
exec jq -c '
  .resourceLogs[] |
  (.resource.attributes // [] | map({(.key): (.value | to_entries[0] | .value)}) | add // {}) as $resAttrs |
  .scopeLogs[] |
  (.scope.name // "") as $scope |
  .logRecords[] |
  {
    timestamp_ns: .timeUnixNano,
    observed_ns: (.observedTimeUnixNano // ""),
    severity_number: (.severityNumber // 0),
    severity: (.severityText // "UNSPECIFIED"),
    body: (.body.stringValue // (.body | tostring)),
    trace_id: (.traceId // ""),
    span_id: (.spanId // ""),
    flags: (.flags // 0),
    scope_name: $scope
  } + $resAttrs
' -- "$2"
