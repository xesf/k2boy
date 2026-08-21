// image.js — BMP and TGA decoders.
//
// Port of src/assets/image.cpp, which decodes 1035/1035 images in the install.
// Output is always tightly packed top-row-first RGBA8, which is what
// GPUQueue.writeTexture wants.
//
// The browser could decode BMP itself via createImageBitmap, and for 24-bit
// files it would. It would not handle the 8-bit paletted files the install is
// full of, and it gives no control over row order. Decoding by hand is 80 lines
// and removes the variable.

import { fs_read_text } from '../core/vfs.js';

const rd16 = (d, o) => d[o] | (d[o + 1] << 8);
const rd32 = (d, o) => (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)) >>> 0;
const rds32 = (d, o) => d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24);

export function image_load(buf) {
    const d = new Uint8Array(buf);
    if (d.length >= 2 && d[0] === 0x42 && d[1] === 0x4d) return image_load_bmp(d);
    // TGA has no magic; accept it if the type byte is one we handle.
    if (d.length >= 18 && (d[2] === 2 || d[2] === 10)) return image_load_tga(d);
    throw new Error('unrecognised image format');
}

// ---------------------------------------------------------------------- BMP

export function image_load_bmp(d) {
    if (d.length < 54) throw new Error('BMP too small');

    const pixel_off = rd32(d, 10);
    const hdr_size = rd32(d, 14);
    if (hdr_size < 40) throw new Error('unsupported BMP header size');

    const w = rds32(d, 18);
    const h_signed = rds32(d, 22);
    const bpp = rd16(d, 28);
    const comp = rd32(d, 30);

    if (comp !== 0) throw new Error('compressed BMP unsupported');
    if (bpp !== 24 && bpp !== 8) throw new Error('BMP bpp not 8 or 24');
    if (w <= 0 || h_signed === 0) throw new Error('bad BMP dimensions');

    // Negative height means the rows are stored top-down.
    const top_down = h_signed < 0;
    const h = top_down ? -h_signed : h_signed;

    // 8-bit images carry a palette between the header and the pixel data.
    const palette_off = 14 + hdr_size;
    if (bpp === 8) {
        let used = rd32(d, 46);
        if (used === 0) used = 256;
        if (palette_off + used * 4 > d.length) throw new Error('BMP palette truncated');
    }

    const row_bytes = (((w * bpp + 31) / 32) | 0) * 4;   // rows pad to 4 bytes
    if (pixel_off + row_bytes * h > d.length) throw new Error('BMP pixel data truncated');

    const rgba = new Uint8Array(w * h * 4);

    for (let y = 0; y < h; ++y) {
        const src_row = top_down ? y : h - 1 - y;        // BMP is bottom-up by default
        let s = pixel_off + row_bytes * src_row;
        let t = y * w * 4;

        if (bpp === 24) {
            for (let x = 0; x < w; ++x, s += 3, t += 4) {
                rgba[t] = d[s + 2];                      // stored BGR
                rgba[t + 1] = d[s + 1];
                rgba[t + 2] = d[s];
                rgba[t + 3] = 255;
            }
        } else {
            for (let x = 0; x < w; ++x, ++s, t += 4) {
                const e = palette_off + d[s] * 4;        // BGRA entries
                rgba[t] = d[e + 2];
                rgba[t + 1] = d[e + 1];
                rgba[t + 2] = d[e];
                rgba[t + 3] = 255;
            }
        }
    }

    return { width: w, height: h, rgba };
}

// ---------------------------------------------------------------------- TGA

export function image_load_tga(d) {
    if (d.length < 18) throw new Error('TGA too small');

    const id_len = d[0];
    const cmap_type = d[1];
    const image_type = d[2];
    const w = rd16(d, 12);
    const h = rd16(d, 14);
    const bpp = d[16];
    const desc = d[17];

    if (cmap_type !== 0) throw new Error('paletted TGA unsupported');
    if (image_type !== 2 && image_type !== 10) throw new Error('TGA type not 2 or 10');
    if (bpp !== 24 && bpp !== 32) throw new Error('TGA bpp not 24 or 32');
    if (w <= 0 || h <= 0) throw new Error('bad TGA dimensions');

    const bytes_pp = bpp >> 3;
    let src = 18 + id_len;
    const pixels = new Uint8Array(w * h * bytes_pp);

    if (image_type === 2) {
        if (src + pixels.length > d.length) throw new Error('TGA data truncated');
        pixels.set(d.subarray(src, src + pixels.length));
    } else {
        // Type 10: run-length encoded. Packet header bit 7 selects RLE vs
        // literal, low 7 bits are (count - 1).
        let written = 0;
        while (written < pixels.length) {
            if (src >= d.length) throw new Error('TGA RLE truncated');
            const packet = d[src++];
            const count = (packet & 0x7f) + 1;
            const bytes = count * bytes_pp;
            if (written + bytes > pixels.length) throw new Error('TGA RLE overflow');

            if (packet & 0x80) {
                if (src + bytes_pp > d.length) throw new Error('TGA RLE truncated');
                for (let i = 0; i < count; ++i)
                    for (let b = 0; b < bytes_pp; ++b)
                        pixels[written + i * bytes_pp + b] = d[src + b];
                src += bytes_pp;
            } else {
                if (src + bytes > d.length) throw new Error('TGA literal truncated');
                pixels.set(d.subarray(src, src + bytes), written);
                src += bytes;
            }
            written += bytes;
        }
    }

    // Bit 5 of the descriptor: set means the first row is the top one.
    const top_down = (desc & 0x20) !== 0;
    const rgba = new Uint8Array(w * h * 4);

    for (let y = 0; y < h; ++y) {
        const src_row = top_down ? y : h - 1 - y;
        let s = src_row * w * bytes_pp;
        let t = y * w * 4;
        for (let x = 0; x < w; ++x, s += bytes_pp, t += 4) {
            rgba[t] = pixels[s + 2];                     // stored BGR(A)
            rgba[t + 1] = pixels[s + 1];
            rgba[t + 2] = pixels[s];
            rgba[t + 3] = bytes_pp === 4 ? pixels[s + 3] : 255;
        }
    }

    return { width: w, height: h, rgba };
}

// ---------------------------------------------------------------------- .ifl

// Material bitmaps sometimes point at an Image File List — one frame filename
// per line — rather than an image. 28 ship, driving animated textures such as
// the torch flames. Until texture animation is wired up, take the first frame,
// which is what the native viewer does (main.cpp::resolve_ifl).
export async function image_resolve_ifl(path) {
    if (!/\.ifl$/i.test(path)) return path;

    const text = await fs_read_text(path);
    if (!text) return path;

    for (const raw of text.split(/\r\n|\r|\n/)) {
        const line = raw.trim();
        if (!line || line[0] === ';') continue;
        return line.split(/\s/)[0];
    }
    return path;
}

// Box-filtered mip chain. WebGPU has no glGenerateMipmap; the alternatives are
// a render pass per level or doing it on the CPU. These textures are 256x256 at
// most and decoded once, so the CPU path is not worth avoiding — and without
// mips the tiled walls alias into noise at any distance.
export function image_build_mips(width, height, rgba) {
    const levels = [{ width, height, rgba }];
    let w = width, h = height, src = rgba;

    while (w > 1 || h > 1) {
        const nw = Math.max(1, w >> 1);
        const nh = Math.max(1, h >> 1);
        const dst = new Uint8Array(nw * nh * 4);

        for (let y = 0; y < nh; ++y) {
            const y0 = Math.min(y * 2, h - 1), y1 = Math.min(y * 2 + 1, h - 1);
            for (let x = 0; x < nw; ++x) {
                const x0 = Math.min(x * 2, w - 1), x1 = Math.min(x * 2 + 1, w - 1);
                const a = (y0 * w + x0) * 4, b = (y0 * w + x1) * 4;
                const c = (y1 * w + x0) * 4, e = (y1 * w + x1) * 4;
                const o = (y * nw + x) * 4;
                for (let k = 0; k < 4; ++k)
                    dst[o + k] = (src[a + k] + src[b + k] + src[c + k] + src[e + k] + 2) >> 2;
            }
        }
        levels.push({ width: nw, height: nh, rgba: dst });
        w = nw; h = nh; src = dst;
    }
    return levels;
}
