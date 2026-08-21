// vfs.js — case-insensitive virtual filesystem over the retail install.
//
// A direct port of src/platform/vfs.cpp, module-level state and all, and
// mandatory for the same reasons: the shipped data mixes `.K2A` / `.k2a` /
// `.K2a` inside one directory, embeds Windows-style backslash paths
// (`geometry\rooms\prisonfix\prison3.k2a`), uses doubled backslashes in .lvl
// [Bodies] entries, and has to resolve on a case-sensitive volume.
//
// The one structural difference: a browser cannot walk a directory. The index
// vfs.cpp builds with opendir at mount time is instead produced ahead of time
// by tools/webindex.py and fetched as web/data-index.txt.
//
// Two backends:
//   fs_mount_http(root, manifest)  — a served copy of the install (default).
//   fs_mount_files(file_list)      — a folder the user picked with
//                                    <input webkitdirectory>. No server and no
//                                    manifest needed, so anyone can point the
//                                    page at their own install.

let g_by_path = new Map();   // normalised relative path -> handle
let g_by_name = new Map();   // lowercased basename       -> handle
let g_cache = new Map();     // normalised path           -> ArrayBuffer
let g_root = '';
let g_bytes_read = 0;

const CODE_A = 'A'.charCodeAt(0), CODE_Z = 'Z'.charCodeAt(0);

// Backslash to slash, collapse repeated separators, strip a leading separator
// and "./", lowercase. Mirrors vfs.cpp::normalise exactly — the two must agree
// or a path that works in the native viewer will 404 here.
export function normalise(path) {
    let out = '';
    let prev_sep = false;
    for (let i = 0; i < path.length; ++i) {
        let c = path[i];
        if (c === '\\') c = '/';
        if (c === '/') {
            if (prev_sep) continue;
            prev_sep = true;
        } else {
            prev_sep = false;
            const code = c.charCodeAt(0);
            if (code >= CODE_A && code <= CODE_Z) c = String.fromCharCode(code + 32);
        }
        out += c;
    }
    if (out.startsWith('/')) out = out.slice(1);
    if (out.startsWith('./')) out = out.slice(2);
    return out;
}

export function path_basename(path) {
    let start = 0;
    for (let i = 0; i < path.length; ++i)
        if (path[i] === '/' || path[i] === '\\') start = i + 1;
    return path.slice(start);
}

export function path_dirname(path) {
    const n = normalise(path);
    const i = n.lastIndexOf('/');
    return i < 0 ? '' : n.slice(0, i);
}

export function path_extension(path) {
    const b = path_basename(path);
    const i = b.lastIndexOf('.');
    return i < 0 ? '' : b.slice(i).toLowerCase();
}

// ---------------------------------------------------------------- mounting

function fs_add(rel, handle) {
    const key = normalise(rel);
    // First writer wins, matching vfs.cpp: data/Rooms duplicates
    // data/Geometry/Rooms and either resolves to a byte-identical file.
    if (!g_by_path.has(key)) g_by_path.set(key, handle);
    const base = path_basename(key);
    if (!g_by_name.has(base)) g_by_name.set(base, handle);
}

function fs_reset() {
    g_by_path = new Map();
    g_by_name = new Map();
    g_cache = new Map();
    g_bytes_read = 0;
}

// Percent-encode each path segment but leave separators alone, so
// "Geometry/Rooms/prisonfix/prison 1.k2a" survives a fetch.
const encode_path = (rel) => rel.split('/').map(encodeURIComponent).join('/');

// `root` is a URL prefix, e.g. '../data/'. `manifest_url` points at the output
// of tools/webindex.py.
export async function fs_mount_http(root, manifest_url) {
    const res = await fetch(manifest_url);
    if (!res.ok) {
        throw new Error(
            `missing ${manifest_url} (HTTP ${res.status}) — ` +
            `run "make webindex" to generate it from your install`);
    }
    const text = await res.text();

    fs_reset();
    g_root = root.endsWith('/') ? root : root + '/';

    for (const line of text.split('\n')) {
        const rel = line.trim();
        if (rel) fs_add(rel, g_root + encode_path(rel));
    }
    if (g_by_path.size === 0) throw new Error('manifest is empty');
    return g_by_path.size;
}

// `file_list` is a FileList from <input type="file" webkitdirectory>. Each File
// carries webkitRelativePath, which includes the picked folder as its first
// segment; that segment is stripped so paths match the manifest form.
export function fs_mount_files(file_list) {
    fs_reset();
    g_root = '';
    for (const f of file_list) {
        const rel = (f.webkitRelativePath || f.name).split('/').slice(1).join('/');
        if (rel) fs_add(rel, f);
    }
    return g_by_path.size;
}

// ------------------------------------------------------------------ lookup

export const fs_resolve = (path) => g_by_path.get(normalise(path)) ?? null;
export const fs_resolve_basename = (name) => g_by_name.get(normalise(path_basename(name))) ?? null;
export const fs_has = (path) => g_by_path.has(normalise(path));
export const fs_file_count = () => g_by_path.size;
export const fs_bytes_read = () => g_bytes_read;

// Every path whose basename ends with `ext` (include the dot).
export function fs_list_by_extension(ext) {
    const suffix = ext.toLowerCase();
    const out = [];
    for (const key of g_by_path.keys())
        if (key.endsWith(suffix)) out.push(key);
    return out;
}

// ----------------------------------------------------------------- reading

async function read_handle(handle) {
    if (typeof handle === 'string') {
        const res = await fetch(handle);
        if (!res.ok) return null;
        return await res.arrayBuffer();
    }
    return await handle.arrayBuffer();     // File / Blob from the directory picker
}

// Path first, then basename — the fallback order used throughout main.cpp,
// because .K2A room names inside a .lvl are bare filenames while material
// bitmaps are absolute authoring paths from the artist's machine
// (`C:\3dsmax\...`) whose only usable part is the basename.
export async function fs_read(path) {
    const key = normalise(path);
    const cached = g_cache.get(key);
    if (cached) return cached;

    const handle = fs_resolve(path) ?? fs_resolve_basename(path);
    if (!handle) return null;

    const buf = await read_handle(handle);
    if (buf) {
        g_bytes_read += buf.byteLength;
        g_cache.set(key, buf);
    }
    return buf;
}

export async function fs_read_text(path) {
    const buf = await fs_read(path);
    // The install is Windows-1252 era ASCII. latin1 never throws, which matters:
    // a few .K2A files carry stray high bytes in comments and strict UTF-8
    // decoding would reject the whole file.
    return buf ? new TextDecoder('latin1').decode(buf) : null;
}

// Frees decoded file bytes. Textures and meshes are on the GPU by the time a
// level finishes loading, so holding the source text costs tens of megabytes
// for nothing.
export function fs_drop_cache() { g_cache = new Map(); }
