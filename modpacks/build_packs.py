#!/usr/bin/env python3
"""Builds the mod packs listed in modpacks/packs.txt and uploads them to the
"mods" release (run by .github/workflows/mod-packs.yml): the missing ones, and
those whose folder changed in the source repository since they were built (a
song added or removed, an asset changed: the game's MOD PACKS screen shows
players what changed, to install or remove). A pack whose line goes from
packs.txt is taken off the release.

Needs git, git-lfs and an authenticated gh. Run with --local <dir> to only
pack a folder that is already on disk (used for testing).

Three kinds of packs:
  <folder>                          mods/<folder> of the "source" repository (Git LFS)
  drive <folder> <zip>=<id> ...     zips already built (by this script's --local mode) and
                                    shared on Google Drive with a public link; they are
                                    copied to the release as they are (quote a folder
                                    name with spaces: drive "My Mod" ...)
  local <folder>                    modpacks/<folder> of this repository: charts made with
                                    the game itself (local "charts made in sniff")"""
import os, re, shlex, shutil, subprocess, sys, zipfile

LIMIT = int(os.environ.get('GH_PACK_LIMIT', 1_900_000_000))  # GitHub allows 2 GiB per release file
STORED = {'.png', '.jpg', '.jpeg', '.ogg', '.mp3', '.webm', '.mp4', '.zip', '.7z', '.flp', '.wav', '.ttf', '.otf'}
TAG = 'mods'
STATE = 'mod-packs-state.txt'  # folder -> what it was built from (the source folder's git tree, or the Drive files)
HERE = os.path.dirname(os.path.abspath(__file__))


def run(*cmd, check=True, capture=False, cwd=None, env=None):
    print('+', ' '.join(cmd), flush=True)
    r = subprocess.run(cmd, check=check, cwd=cwd, env=env, text=True,
                       stdout=subprocess.PIPE if capture else None)
    return r.stdout if capture else r.returncode


def slug(name):
    s = re.sub(r'[^a-z0-9]+', '-', name.lower()).strip('-')
    return s or 'mod'


def read_list():
    source, commit, folders, drive, local = None, None, [], {}, set()
    for line in open(os.path.join(HERE, 'packs.txt'), encoding='utf-8'):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        if line.startswith('source '):
            _, source, commit = line.split()
        elif line.startswith('drive '):
            _, folder, *zips = shlex.split(line)  # a folder name with spaces is quoted
            drive[folder] = [tuple(z.split('=', 1)) for z in zips]
            folders.append(folder)
        elif line.startswith('local '):
            _, folder = shlex.split(line)
            if not os.path.isdir(os.path.join(HERE, folder)):
                raise SystemExit('no folder modpacks/%s for: %s' % (folder, line))
            local.add(folder)
            folders.append(folder)
        else:
            folders.append(line)
    return source, commit, folders, drive, local


def fetch_drive(file_id, out):
    """Downloads a publicly shared Google Drive file (large files skip the virus-scan page)."""
    url = 'https://drive.usercontent.google.com/download?id=%s&export=download&confirm=t' % file_id
    run('curl', '-sS', '-f', '-L', '--retry', '5', '--retry-all-errors', '-o', out, url)
    with open(out, 'rb') as fh:
        if fh.read(4) != b'PK\x03\x04':
            raise SystemExit('not a zip (is the Drive file shared with "anyone with the link"?): ' + file_id)


def pack(folder_path, folder_name, out_dir):
    """Zips folder_path as <folder_name>/..., split into parts under LIMIT."""
    files = []
    for dp, dn, fn in os.walk(folder_path):
        dn.sort()
        for f in sorted(fn):
            p = os.path.join(dp, f)
            with open(p, 'rb') as fh:
                if fh.read(24).startswith(b'version https://git-lfs'):
                    raise SystemExit('LFS content missing for ' + p)
            files.append(p)
    parts, zf, fh, n = [], None, None, 0
    base = 'GodsHand-mod-' + slug(folder_name)

    def open_part():
        nonlocal zf, fh, n
        n += 1
        path = os.path.join(out_dir, '%s-part%d.zip' % (base, n))
        fh = open(path, 'wb')
        zf = zipfile.ZipFile(fh, 'w', allowZip64=True)
        parts.append(path)

    open_part()
    count = 0
    for p in files:
        size = os.path.getsize(p)
        ext = os.path.splitext(p)[1].lower()
        stored = ext in STORED
        estimate = size if stored else size // 4
        if count and fh.tell() + estimate > LIMIT:
            zf.close(); fh.close()
            open_part()
            count = 0
        arc = folder_name + '/' + os.path.relpath(p, folder_path).replace(os.sep, '/')
        zf.write(p, arc, compress_type=zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED, compresslevel=6)
        count += 1
    zf.close(); fh.close()
    if len(parts) == 1:
        single = os.path.join(out_dir, base + '.zip')
        os.replace(parts[0], single)
        parts = [single]
    for p in parts:
        if os.path.getsize(p) > 2_147_000_000:
            raise SystemExit('pack too big: ' + p)
    return parts


NOTES = (
    "Mod packs for God's Hand. Download the zips you want and put them in the game's mods/ folder: "
    "the game unpacks them the next time it starts (or press R in Freeplay). Packs split into -part1, "
    "-part2... must all be downloaded.\n\n"
    "Not all of these mods are by deatuamae67-crypto: some are, and the others belong to their original "
    "creators, who keep every right to them. They are shared so they can be played in God's Hand, and "
    "they are not covered by the game's license.")


def main():
    if len(sys.argv) > 2 and sys.argv[1] == '--local':
        out = sys.argv[3] if len(sys.argv) > 3 else '.'
        for p in pack(sys.argv[2], os.path.basename(sys.argv[2].rstrip('/')), out):
            print(p, os.path.getsize(p))
        return
    source, commit, folders, drive, local = read_list()
    work = os.path.abspath('modpack-work')
    os.makedirs(work, exist_ok=True)
    if run('gh', 'release', 'view', TAG, check=False) != 0:
        run('gh', 'release', 'create', TAG, '--title', "God's Hand - mod packs", '--latest=false',
            '--target', os.environ.get('GITHUB_SHA', 'HEAD'), '--notes', NOTES)
    else:
        run('gh', 'release', 'edit', TAG, '--notes', NOTES)  # the text follows this file
    have = set(run('gh', 'release', 'view', TAG, '--json', 'assets', '-q', '.assets[].name', capture=True).split())

    def read_tsv(name):
        table = {}
        if name in have:
            run('gh', 'release', 'download', TAG, '-p', name, '-D', work, '--clobber')
            for line in open(os.path.join(work, name), encoding='utf-8'):
                if '\t' in line:
                    key, value = line.rstrip('\n').split('\t', 1)
                    table[key] = value
        return table

    manifest = {k: v.split() for k, v in read_tsv('mod-packs.txt').items()}
    state = read_tsv(STATE)

    # What each pack is built from now: the source folder's git tree (no file
    # content is needed for it), its Drive files, or this repository's folder's tree.
    repo = os.path.join(work, 'src')
    env = dict(os.environ, GIT_LFS_SKIP_SMUDGE='1')
    if any(f not in drive and f not in local for f in folders) and not os.path.isdir(repo):
        run('git', 'clone', '--filter=blob:none', '--no-checkout', 'https://github.com/%s.git' % source, repo, env=env)
        run('git', 'sparse-checkout', 'init', '--no-cone', cwd=repo, env=env)
    current = {}
    for f in folders:
        if f in drive:
            current[f] = 'drive:' + ' '.join('%s=%s' % z for z in drive[f])
        elif f in local:
            tree = (run('git', 'rev-parse', 'HEAD:modpacks/' + f, capture=True, check=False, cwd=HERE) or '').strip()
            current[f] = 'local:' + tree if tree else ''
        else:
            tree = (run('git', 'rev-parse', '%s:mods/%s' % (commit, f), capture=True, check=False, cwd=repo) or '').strip()
            current[f] = 'git:' + tree if tree else ''

    def published(f):
        return f in manifest and all(n in have for n in manifest[f])

    for f in folders:
        if published(f) and f not in state and current[f]:
            state[f] = current[f]  # built before the state was kept: as it is now
    todo = [f for f in folders if not published(f) or (current[f] and state.get(f) != current[f])]
    gone = [f for f in manifest if f not in folders]
    print('packs to build:', todo or 'none', flush=True)
    print('packs to take off the release:', gone or 'none', flush=True)

    def write_manifest():
        with open(os.path.join(work, 'mod-packs.txt'), 'w', encoding='utf-8') as m:
            for f in folders:
                if f in manifest:
                    m.write(f + '\t' + ' '.join(manifest[f]) + '\n')
        with open(os.path.join(work, STATE), 'w', encoding='utf-8') as m:
            for f in folders:
                if f in state and f in manifest:
                    m.write(f + '\t' + state[f] + '\n')
        run('gh', 'release', 'upload', TAG, os.path.join(work, 'mod-packs.txt'), os.path.join(work, STATE), '--clobber')

    def drop_unused(names):
        """Release files no pack lists any more (a pack split differently, or gone)."""
        used = {n for f in manifest for n in manifest[f]}
        for n in names:
            if n not in used and n in have:
                run('gh', 'release', 'delete-asset', TAG, n, '-y', check=False)
                have.discard(n)

    if gone:
        old = [n for f in gone for n in manifest[f]]
        for f in gone:
            del manifest[f]
            state.pop(f, None)
        write_manifest()
        drop_unused(old)

    for folder in [f for f in todo if f in drive]:
        names = []
        for name, file_id in drive[folder]:
            out = os.path.join(work, name)
            fetch_drive(file_id, out)
            run('gh', 'release', 'upload', TAG, out, '--clobber')
            have.add(name)
            os.remove(out)
            names.append(name)
        old = manifest.get(folder, [])
        manifest[folder] = names
        state[folder] = current[folder]
        write_manifest()
        drop_unused(old)
    for folder in [f for f in todo if f in local]:
        # Its media are in Git LFS (.gitattributes): the checkout has their pointers.
        run('git', 'lfs', 'pull', '--include', 'modpacks/' + folder + '/**', cwd=os.path.dirname(HERE))
        parts = pack(os.path.join(HERE, folder), folder, work)
        for p in parts:
            run('gh', 'release', 'upload', TAG, p, '--clobber')
            have.add(os.path.basename(p))
            os.remove(p)
        old = manifest.get(folder, [])
        manifest[folder] = [os.path.basename(p) for p in parts]
        state[folder] = current[folder]
        write_manifest()
        drop_unused(old)
    todo = [f for f in todo if f not in drive and f not in local]
    if todo:
        for folder in todo:
            run('git', 'sparse-checkout', 'set', '--no-cone', '/mods/' + folder + '/', cwd=repo, env=env)
            run('git', 'checkout', '-q', commit, cwd=repo, env=env)
            run('git', 'lfs', 'pull', '--include', 'mods/' + folder + '/**', cwd=repo)
            parts = pack(os.path.join(repo, 'mods', folder), folder, work)
            for p in parts:
                run('gh', 'release', 'upload', TAG, p, '--clobber')
                have.add(os.path.basename(p))
                os.remove(p)
            old = manifest.get(folder, [])
            manifest[folder] = [os.path.basename(p) for p in parts]
            state[folder] = current[folder]
            write_manifest()
            drop_unused(old)
            # Free the disk before the next mod.
            shutil.rmtree(os.path.join(repo, 'mods', folder), ignore_errors=True)
            shutil.rmtree(os.path.join(repo, '.git', 'lfs', 'objects'), ignore_errors=True)
    if not todo and not gone and STATE not in have:
        write_manifest()  # the state, kept from now on


if __name__ == '__main__':
    main()
