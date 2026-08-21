// lvl.js — parser for the level description (.lvl).
//
// Port of src/assets/lvl.cpp, validated on all 20 shipped levels. Two parts: a
// one-line command line, then INI-style bracketed sections. See
// docs/formats/lvl.md.
//
// `;` starts a comment. Designers disabled entries by commenting them rather
// than deleting, so commented lines are common and must be skipped — including
// whole [Prince] start points and, in palace2, the [ClipPlanes] line.

// Strips ';' comments and surrounding whitespace, drops anything left empty.
function split_lines(text) {
    const out = [];
    for (const raw of text.split(/\r\n|\r|\n/)) {
        const c = raw.indexOf(';');
        const line = (c >= 0 ? raw.slice(0, c) : raw).trim();
        if (line) out.push(line);
    }
    return out;
}

// Splits on commas and whitespace alike: shipped levels use both, sometimes in
// the same line ("0 22,0.01,1.92,83.25").
const split_values = (s) => s.split(/[,\s]+/).filter((x) => x.length > 0);

const to_f = (s) => { const v = parseFloat(s); return Number.isNaN(v) ? 0 : v; };
const to_i = (s) => { const v = parseInt(s, 10); return Number.isNaN(v) ? 0 : v; };

const region_new = (name) => ({ name, prince_starts: [], rooms: [] });

export function lvl_parse(text) {
    const out = {
        name: '',
        command_line: '',
        bodies: [],
        regions: [],
        items: [],
        fog: [],
        sky_color: [0, 0, 0],
        clip_near: 0.1,
        clip_far: 1000,
        has_sky_color: false,
        has_clip_planes: false,
    };

    const lines = split_lines(text);
    if (lines.length === 0) throw new Error('no content');

    // First non-comment line is the command line; its first bare token is the
    // level name. The flags are decoded in docs/formats/lvl.md, but the section
    // data below is what a renderer actually needs.
    out.command_line = lines[0];
    {
        const tok = split_values(lines[0]);
        if (tok.length && tok[0][0] !== '-') out.name = tok[0];
    }

    let section = '';
    for (let i = 1; i < lines.length; ++i) {
        const line = lines[i];

        if (line.length >= 3 && line[0] === '[' && line[line.length - 1] === ']') {
            section = line.slice(1, -1);
            // A [Region] opens a new group; the [Prince] and [Rooms] that follow
            // belong to it. Levels that omit [Region] still need somewhere to
            // put their rooms, so one is synthesised on demand below.
            if (section === 'Region' && i + 1 < lines.length)
                out.regions.push(region_new(lines[++i]));
            continue;
        }

        switch (section) {
            case 'Bodies': {
                const v = split_values(line);
                // A lone number is the count; ignore it and trust the line count.
                if (v.length >= 2) out.bodies.push({ index: to_i(v[0]), path: v[1] });
                break;
            }
            case 'Prince': {
                const v = split_values(line);
                if (v.length >= 4) {
                    if (!out.regions.length) out.regions.push(region_new(''));
                    out.regions[out.regions.length - 1].prince_starts.push({
                        index: to_i(v[0]),
                        x: to_f(v[1]), y: to_f(v[2]), z: to_f(v[3]),
                        heading: v.length >= 5 ? to_f(v[4]) : 0,
                    });
                }
                break;
            }
            case 'Rooms': {
                // Skip the leading count; room names never start with a digit.
                if (/^\d+$/.test(line)) break;
                if (!out.regions.length) out.regions.push(region_new(''));
                out.regions[out.regions.length - 1].rooms.push(line);
                break;
            }
            case 'Items':
                out.items.push(line);
                break;
            case 'Fog': {
                const v = split_values(line);
                if (v.length >= 4) {
                    out.fog.push({
                        distance: to_f(v[0]),
                        r: to_f(v[1]), g: to_f(v[2]), b: to_f(v[3]),
                        flag: v.length >= 5 ? to_i(v[4]) : 0,
                    });
                }
                break;
            }
            case 'SkyColor': {
                const v = split_values(line);
                if (v.length >= 3) {
                    out.sky_color = [to_f(v[0]), to_f(v[1]), to_f(v[2])];
                    out.has_sky_color = true;
                }
                break;
            }
            case 'ClipPlanes': {
                const v = split_values(line);
                if (v.length >= 2) {
                    out.clip_near = to_f(v[0]);
                    out.clip_far = to_f(v[1]);
                    out.has_clip_planes = true;
                }
                break;
            }
            default:
                // SkyDome, TextureAnimation, PrinceMaterial, RotateSkyDome,
                // Enemy ... fall through unhandled.
                break;
        }
    }

    return out;
}

// Returns the geometry filename for a [Rooms] entry, appending ".k2a" only when
// the entry does not already carry an extension. Some levels write a bare name
// ("prison1"), others include it ("AltarChamber.k2a").
export function lvl_room_filename(room) {
    const dot = room.lastIndexOf('.');
    const slash = Math.max(room.lastIndexOf('/'), room.lastIndexOf('\\'));
    return dot > slash && dot >= 0 ? room : room + '.k2a';
}

// [Fog] rows are `factor, r, g, b, flag`, and the first column is NOT a
// distance — it is a multiplier on the [ClipPlanes] far value. Every shipped
// level writes 1.5 in the second row, so fog saturates half again beyond the
// far plane. Reading these as metres put fog end at 1.5 units and drowned every
// level in its own fog colour; see docs/formats/lvl.md.
//
// palace2 comments out [ClipPlanes], so a fallback is needed.
export function lvl_fog_range(lvl, fallback_far = 100) {
    const far = lvl.has_clip_planes && lvl.clip_far > 1 ? lvl.clip_far : fallback_far;
    if (lvl.fog.length < 2) return null;
    // Colour comes from the second row, matching the native renderer: both rows
    // carry the same colour in every shipped level, but the far row is the one
    // the fog saturates to.
    return {
        start: lvl.fog[0].distance * far,
        end: lvl.fog[1].distance * far,
        color: [lvl.fog[1].r / 255, lvl.fog[1].g / 255, lvl.fog[1].b / 255],
        far,
    };
}
