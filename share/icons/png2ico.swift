// Packs square PNGs into a Windows .ico, which since Windows Vista may hold
// PNGs as they are. Each one's size goes in a byte, where 0 means 256.
//
//   swift png2ico.swift <out.ico> <in.png>...
import Foundation
let args = CommandLine.arguments
let pngs = args.dropFirst(2).map { try! Data(contentsOf: URL(fileURLWithPath: $0)) }

var ico = Data()
func append<T: FixedWidthInteger>(_ value: T) {
    withUnsafeBytes(of: value.littleEndian) { ico.append(contentsOf: $0) }
}
// The width of a PNG is in its IHDR chunk, at byte 16.
func width(_ png: Data) -> Int {
    png.subdata(in: 16..<20).reduce(0) { $0 << 8 | Int($1) }
}

append(UInt16(0)) // reserved
append(UInt16(1)) // an icon, not a cursor
append(UInt16(pngs.count))
var offset = 6 + 16 * pngs.count
for png in pngs {
    let size = width(png)
    append(UInt8(size % 256)) // width
    append(UInt8(size % 256)) // height
    append(UInt8(0)) // no palette
    append(UInt8(0)) // reserved
    append(UInt16(1)) // colour planes
    append(UInt16(32)) // bits per pixel
    append(UInt32(png.count))
    append(UInt32(offset))
    offset += png.count
}
for png in pngs {
    ico.append(png)
}
try! ico.write(to: URL(fileURLWithPath: args[1]))
