// midisend_ts: send a note sequence to a CoreMIDI destination with every event TIME STAMPED, so the MIDI server (a real time
// thread) does the timing, not this process. The old build/paraphony/device/tools/midisend slept between events with usleep and
// sent each event "now": on a loaded Mac the sleeps ran 3 to 5 times long, which is what made the
// unit look like it read one USB MIDI event per 195 ms.
//   midisend_ts list
//   midisend_ts DEST "on 64 100; wait 13; on 67; ...; off 64"   (channel 1, velocity optional, wait in ms)
// Same syntax and the same 5 ms after every command as midisend, so a sequence means the same gaps. Prints each event with its
// planned time in ms. Stays alive until the last event is due plus 300 ms.
import CoreMIDI
import Foundation

func name(_ e: MIDIEndpointRef) -> String {
    var s: Unmanaged<CFString>?
    MIDIObjectGetStringProperty(e, kMIDIPropertyDisplayName, &s)
    return (s?.takeRetainedValue() as String?) ?? "?"
}
let args = CommandLine.arguments
let n = MIDIGetNumberOfDestinations()
if args.count < 2 || args[1] == "list" {
    for i in 0..<n { print(i, name(MIDIGetDestination(i))) }
    exit(0)
}
var dest: MIDIEndpointRef = 0
for i in 0..<n where name(MIDIGetDestination(i)).lowercased().contains(args[1].lowercased()) { dest = MIDIGetDestination(i); break }
if dest == 0 { FileHandle.standardError.write("no destination matching \(args[1])\n".data(using: .utf8)!); exit(2) }
var client = MIDIClientRef(); MIDIClientCreate("midisend_ts" as CFString, nil, nil, &client)
var port = MIDIPortRef(); MIDIOutputPortCreate(client, "out" as CFString, &port)

var tb = mach_timebase_info_data_t(); mach_timebase_info(&tb)
func msToTicks(_ ms: Double) -> UInt64 { return UInt64(ms * 1e6 * Double(tb.denom) / Double(tb.numer)) }
let lead = 50.0                       // ms between now and the first event: room for the sends below
let base = mach_absolute_time() + msToTicks(lead)

let stepGap = Double(ProcessInfo.processInfo.environment["MIDISEND_GAP_MS"] ?? "5")!
var t = 0.0                            // planned time of the next event, ms after base
func send(_ bytes: [UInt8], _ at: Double) {
    var buf = [UInt8](repeating: 0, count: 512)
    buf.withUnsafeMutableBytes { raw in
        let list = raw.baseAddress!.assumingMemoryBound(to: MIDIPacketList.self)
        let p = MIDIPacketListInit(list)
        _ = MIDIPacketListAdd(list, 512, p, base + msToTicks(at), bytes.count, bytes)
        MIDISend(port, dest, list)
    }
}
for cmd in args[2].split(separator: ";") {
    let w = cmd.split(separator: " ").map(String.init)
    guard let op = w.first else { continue }
    switch op {
    case "on": send([0x90, UInt8(w[1])!, UInt8(w.count > 2 ? w[2] : "100")!], t); print(String(format: "on %@ t %.1f", w[1], t))
    case "off": send([0x80, UInt8(w[1])!, 0], t); print(String(format: "off %@ t %.1f", w[1], t))
    case "wait": t += Double(w[1])!
    default: print("unknown", op)
    }
    t += stepGap
}
// wait (coarse sleeps are fine here, the events are already scheduled) until the last one is due, plus 300 ms
let end = base + msToTicks(t + 300)
while mach_absolute_time() < end { usleep(20000) }
