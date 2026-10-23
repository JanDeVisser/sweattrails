// Renders an SVG to a square PNG, keeping its transparency, which qlmanage
// doesn't. AppKit reads SVG since macOS 14.
//
//   swift svg2png.swift <in.svg> <out.png> <size>
import AppKit
let args = CommandLine.arguments
let size = Int(args[3])!
guard let image = NSImage(contentsOf: URL(fileURLWithPath: args[1])) else { fatalError("can't read \(args[1])") }
let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size, bitsPerSample: 8,
    samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
image.draw(in: NSRect(x: 0, y: 0, width: size, height: size))
NSGraphicsContext.restoreGraphicsState()
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: args[2]))
