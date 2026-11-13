// midi_probe_dest: a virtual CoreMIDI destination "MSProbe" that prints every event with its arrival time (ms since the
// first) and its own time stamp. Mac only, no device: point a sender at "MSProbe" to see the gaps it really produces.
// Build: swiftc -O midi_probe_dest.swift -o midi_probe_dest
//   midi_probe_dest SECONDS
import CoreMIDI
import Foundation

let secs = Double(CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "5")!
var client = MIDIClientRef(); MIDIClientCreate("midi_probe_dest" as CFString, nil, nil, &client)
var dest = MIDIEndpointRef()
var t0: UInt64 = 0
var ts0: Double = 0
func now() -> UInt64 { return DispatchTime.now().uptimeNanoseconds }
let st = MIDIDestinationCreateWithBlock(client, "MSProbe" as CFString, &dest) { list, _ in
    let t = now()
    if t0 == 0 { t0 = t }
    var p = list.pointee.packet
    for _ in 0..<list.pointee.numPackets {
        let b = Mirror(reflecting: p.data).children.map { $0.value as! UInt8 }
        let kind = (b[0] & 0xF0) == 0x90 ? "on" : ((b[0] & 0xF0) == 0x80 ? "off" : "x")
        // ts: the packet's own time stamp (0 = now), in ms of the host clock, relative to the first stamped packet
        var tb = mach_timebase_info_data_t(); mach_timebase_info(&tb)
        let tsNs = Double(p.timeStamp) * Double(tb.numer) / Double(tb.denom)
        if ts0 == 0 && p.timeStamp != 0 { ts0 = tsNs }
        print(String(format: "%@ %d t %.2f ts %.2f", kind, b[1], Double(t - t0) / 1e6, p.timeStamp == 0 ? -1.0 : (tsNs - ts0) / 1e6))
        fflush(stdout)
        p = MIDIPacketNext(&p).pointee
    }
}
if st != 0 { print("create failed", st); exit(1) }
RunLoop.current.run(until: Date(timeIntervalSinceNow: secs))
