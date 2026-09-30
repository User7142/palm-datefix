// render.swift - draws the DateFix launcher icon in every size (CoreGraphics)
//
//   swift tools/icon/render.swift <outdir>
//
// writes <outdir>/<name>.ppm (binary P6, RGB on white) and <name>.png:
//   large-72     32x22   Palm OS 3.5 .. 5, 160x160 screens
//   large-144    64x44   Palm OS 5, double density (320x320, 320x480)
//   small-72     15x9    list view
//   small-144    30x18
//
// The icon is a calendar page with the year 2032, the first year after the
// Palm OS limit. The low-density sizes are pixel-aligned (3x5 pixel digits),
// the double-density one uses a real typeface.

import CoreGraphics
import CoreText
import Foundation
import ImageIO
import UniformTypeIdentifiers

struct Canvas {
    let w: Int, h: Int
    let ctx: CGContext

    init(_ w: Int, _ h: Int) {
        self.w = w
        self.h = h
        ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                        space: CGColorSpaceCreateDeviceRGB(),
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        ctx.setFillColor(CGColor(red: 1, green: 1, blue: 1, alpha: 1))
        ctx.fill(CGRect(x: 0, y: 0, width: w, height: h))
        ctx.translateBy(x: 0, y: CGFloat(h))            // y runs downwards
        ctx.scaleBy(x: 1, y: -1)
    }

    func rgb() -> [UInt8] {
        let data = ctx.data!.assumingMemoryBound(to: UInt8.self)
        var out: [UInt8] = []
        for i in 0..<(w * h) {
            out += [data[i * 4], data[i * 4 + 1], data[i * 4 + 2]]
        }
        return out
    }

    func save(_ dir: String, _ name: String) {
        let ppm = Data("P6\n\(w) \(h)\n255\n".utf8) + Data(rgb())
        try! ppm.write(to: URL(fileURLWithPath: "\(dir)/\(name).ppm"))
        let image = ctx.makeImage()!
        let dest = CGImageDestinationCreateWithURL(URL(fileURLWithPath: "\(dir)/\(name).png") as CFURL,
                                                   UTType.png.identifier as CFString, 1, nil)!
        CGImageDestinationAddImage(dest, image, nil)
        CGImageDestinationFinalize(dest)
    }
}

func color(_ r: Int, _ g: Int, _ b: Int, _ a: CGFloat = 1) -> CGColor {
    CGColor(red: CGFloat(r) / 255, green: CGFloat(g) / 255, blue: CGFloat(b) / 255, alpha: a)
}

let navy = color(27, 46, 94)
let blueTop = color(86, 150, 232)
let blueBottom = color(36, 84, 172)
let outline = color(30, 50, 100)

func gradient(_ ctx: CGContext, _ rect: CGRect, _ top: CGColor, _ bottom: CGColor) {
    let g = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: [top, bottom] as CFArray,
                       locations: [0, 1])!
    ctx.saveGState()
    ctx.clip(to: rect)
    ctx.drawLinearGradient(g, start: CGPoint(x: rect.midX, y: rect.minY),
                           end: CGPoint(x: rect.midX, y: rect.maxY), options: [])
    ctx.restoreGState()
}

// 3x5 pixel font for the low-density sizes
let digits: [Character: [String]] = [
    "0": ["###", "#.#", "#.#", "#.#", "###"],
    "2": ["###", "..#", "###", "#..", "###"],
    "3": ["###", "..#", "###", "..#", "###"],
]

func pixelText(_ ctx: CGContext, _ s: String, x: CGFloat, y: CGFloat, pixel: CGFloat, _ c: CGColor) {
    ctx.setFillColor(c)
    var cx = x
    for ch in s {
        for (r, row) in digits[ch]!.enumerated() {
            for (col, px) in row.enumerated() where px == "#" {
                ctx.fill(CGRect(x: cx + CGFloat(col) * pixel, y: y + CGFloat(r) * pixel,
                                width: pixel, height: pixel))
            }
        }
        cx += 4 * pixel
    }
}

/// The green "works beyond 2031" badge as pixel art: a 7x7 disc with a white
/// check mark and a white halo that clears the page lines below it.
func pixelBadge(_ ctx: CGContext, x: CGFloat, y: CGFloat, pixel p: CGFloat) {
    let disc = ["..###..", ".#####.", "#######", "#######", "#######", ".#####.", "..###.."]
    let top = color(124, 216, 100), mid = color(58, 176, 72), bottom = color(30, 138, 58)
    let check: Set<String> = ["3,1", "4,2", "3,3", "2,4", "1,5"]       // "row,col"
    // halo: the disc grown by one pixel
    ctx.setFillColor(color(255, 255, 255))
    for r in -1...7 {
        for c in -1...7 {
            let dr = CGFloat(r) - 3, dc = CGFloat(c) - 3
            if dr * dr + dc * dc <= 4.6 * 4.6 - 0.3 {
                ctx.fill(CGRect(x: x + CGFloat(c) * p, y: y + CGFloat(r) * p, width: p, height: p))
            }
        }
    }
    for (r, row) in disc.enumerated() {
        for (c, ch) in row.enumerated() where ch == "#" {
            ctx.setFillColor(check.contains("\(r),\(c)") ? color(255, 255, 255)
                             : (r <= 1 ? top : (r <= 3 ? mid : bottom)))
            ctx.fill(CGRect(x: x + CGFloat(c) * p, y: y + CGFloat(r) * p, width: p, height: p))
        }
    }
}

// MARK: - large icon, drawn in a 64x44 design grid

func large(_ w: Int, _ h: Int, pixelDigits: Bool) -> Canvas {
    let cv = Canvas(w, h)
    let ctx = cv.ctx
    let k = CGFloat(w) / 64
    ctx.scaleBy(x: k, y: k)

    let page = CGRect(x: 6, y: 6, width: 52, height: 36)
    let r: CGFloat = pixelDigits ? 2 : 4

    // soft shadow
    ctx.saveGState()
    ctx.setShadow(offset: CGSize(width: 0, height: -1.5 * (pixelDigits ? 0 : 1)), blur: pixelDigits ? 0 : 4,
                  color: color(0, 0, 0, 0.35))
    ctx.setFillColor(color(255, 255, 255))
    ctx.addPath(CGPath(roundedRect: page, cornerWidth: r, cornerHeight: r, transform: nil))
    ctx.fillPath()
    ctx.restoreGState()
    if pixelDigits {                                     // hard 1px shadow, pixel-aligned
        ctx.setFillColor(color(150, 160, 180))
        ctx.addPath(CGPath(roundedRect: page.offsetBy(dx: 2, dy: 2).intersection(CGRect(x: 0, y: 0, width: 64, height: 44)),
                           cornerWidth: r, cornerHeight: r, transform: nil))
        ctx.fillPath()
        ctx.setFillColor(color(255, 255, 255))
        ctx.addPath(CGPath(roundedRect: page, cornerWidth: r, cornerHeight: r, transform: nil))
        ctx.fillPath()
    }

    // header band
    ctx.saveGState()
    ctx.addPath(CGPath(roundedRect: page, cornerWidth: r, cornerHeight: r, transform: nil))
    ctx.clip()
    gradient(ctx, CGRect(x: 6, y: 6, width: 52, height: 10), blueTop, blueBottom)
    ctx.setFillColor(color(255, 255, 255, 0.35))          // gloss line
    ctx.fill(CGRect(x: 6, y: 6, width: 52, height: pixelDigits ? 2 : 1.2))
    ctx.restoreGState()

    // outline
    ctx.setStrokeColor(outline)
    ctx.setLineWidth(pixelDigits ? 2 : 1.6)
    ctx.addPath(CGPath(roundedRect: page.insetBy(dx: pixelDigits ? 1 : 0.8, dy: pixelDigits ? 1 : 0.8),
                       cornerWidth: r, cornerHeight: r, transform: nil))
    ctx.strokePath()

    // binder rings
    for x in [16.0, 44.0] as [CGFloat] {
        let ring = CGRect(x: x, y: 2, width: 4, height: 10)
        ctx.addPath(CGPath(roundedRect: ring, cornerWidth: 2, cornerHeight: 2, transform: nil))
        ctx.setFillColor(color(215, 222, 235))
        ctx.fillPath()
        ctx.addPath(CGPath(roundedRect: ring, cornerWidth: 2, cornerHeight: 2, transform: nil))
        ctx.setStrokeColor(outline)
        ctx.setLineWidth(pixelDigits ? 1 : 1)
        ctx.strokePath()
    }

    // the year
    if pixelDigits {
        pixelText(ctx, "2032", x: 16, y: 24, pixel: 2, navy)
        pixelBadge(ctx, x: 48, y: 30, pixel: 2)
    } else {
        let font = CTFontCreateWithName("HelveticaNeue-Bold" as CFString, 14, nil)
        let attr = [kCTFontAttributeName: font, kCTForegroundColorAttributeName: navy] as CFDictionary
        let line = CTLineCreateWithAttributedString(CFAttributedStringCreate(nil, "2032" as CFString, attr)!)
        let width = CTLineGetTypographicBounds(line, nil, nil, nil)
        ctx.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
        ctx.textPosition = CGPoint(x: 32 - CGFloat(width) / 2, y: 32.4)   // centred on the page, a little above the middle
        CTLineDraw(line, ctx)

        // green badge: works beyond 2031
        let c = CGPoint(x: 53.5, y: 35.5)
        ctx.saveGState()
        ctx.setShadow(offset: CGSize(width: 0, height: -1), blur: 2.5, color: color(0, 0, 0, 0.4))
        ctx.setFillColor(color(255, 255, 255))
        ctx.fillEllipse(in: CGRect(x: c.x - 8.6, y: c.y - 8.6, width: 17.2, height: 17.2))
        ctx.restoreGState()
        let g = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(),
                           colors: [color(120, 214, 96), color(34, 150, 62)] as CFArray, locations: [0, 1])!
        ctx.saveGState()
        ctx.addEllipse(in: CGRect(x: c.x - 7.2, y: c.y - 7.2, width: 14.4, height: 14.4))
        ctx.clip()
        ctx.drawLinearGradient(g, start: CGPoint(x: c.x, y: c.y - 7.2), end: CGPoint(x: c.x, y: c.y + 7.2), options: [])
        ctx.restoreGState()
        ctx.setStrokeColor(color(255, 255, 255))
        ctx.setLineWidth(2.6)
        ctx.setLineCap(.round)
        ctx.setLineJoin(.round)
        ctx.move(to: CGPoint(x: c.x - 3.6, y: c.y + 0.2))
        ctx.addLine(to: CGPoint(x: c.x - 0.8, y: c.y + 3.0))
        ctx.addLine(to: CGPoint(x: c.x + 3.8, y: c.y - 2.8))
        ctx.strokePath()
    }
    return cv
}

// MARK: - small icon, 15x9 pixels (x2 for double density)

func small(_ scale: Int) -> Canvas {
    let cv = Canvas(15 * scale, 9 * scale)
    let ctx = cv.ctx
    let k = CGFloat(scale)
    ctx.scaleBy(x: k, y: k)
    ctx.setFillColor(color(255, 255, 255))
    ctx.fill(CGRect(x: 1, y: 0, width: 13, height: 9))
    ctx.setFillColor(blueBottom)
    ctx.fill(CGRect(x: 1, y: 0, width: 13, height: 3))
    ctx.setFillColor(blueTop)
    ctx.fill(CGRect(x: 2, y: 0, width: 11, height: 1))
    ctx.setFillColor(outline)                              // frame
    ctx.fill(CGRect(x: 1, y: 8, width: 13, height: 1))
    ctx.fill(CGRect(x: 1, y: 0, width: 1, height: 9))
    ctx.fill(CGRect(x: 13, y: 0, width: 1, height: 9))
    pixelText(ctx, "32", x: 4, y: 3, pixel: 1, navy)
    return cv
}

let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "."
try? FileManager.default.createDirectory(atPath: out, withIntermediateDirectories: true)
large(32, 22, pixelDigits: true).save(out, "large-72")
large(64, 44, pixelDigits: false).save(out, "large-144")
small(1).save(out, "small-72")
small(2).save(out, "small-144")
print("ok")
