// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// The updater (SHA-256, versions, GitHub's release JSON, staging and putting
// an update in place) and the mods kept in step with their zips (records,
// zip directories read from their end, changes installed and removed again,
// the mods release compared with what is installed).
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "miniz.h"
#include "mod_sync.hpp"
#include "sha256.hpp"
#include "updater.hpp"

using namespace gh;
namespace fs = std::filesystem;

extern int g_fail, g_pass;
#define UCHECK(cond)                                                             \
    do {                                                                         \
        if (cond) ++g_pass;                                                      \
        else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

namespace {

std::string str(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}
std::string get(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
std::string zipBytes(const std::vector<std::pair<std::string, std::string>>& files, bool zip64 = false) {
    mz_zip_archive z{};
    mz_zip_writer_init_heap_v2(&z, 0, 0, zip64 ? MZ_ZIP_FLAG_WRITE_ZIP64 : 0);
    for (const auto& f : files) mz_zip_writer_add_mem(&z, f.first.c_str(), f.second.data(), f.second.size(), MZ_DEFAULT_COMPRESSION);
    void* buf = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
    std::string out(static_cast<const char*>(buf), size);
    mz_free(buf);
    mz_zip_writer_end(&z);
    return out;
}
void makeZip(const fs::path& out, const std::vector<std::pair<std::string, std::string>>& files) {
    fs::create_directories(out.parent_path());
    std::ofstream(out, std::ios::binary) << zipBytes(files);
}

void testSha256() {
    auto hex = [](const std::string& s) {
        Sha256 h;
        h.update(s.data(), s.size());
        return h.finishHex();
    };
    UCHECK(hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    UCHECK(hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    UCHECK(hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // A million 'a', fed in uneven pieces: the same as all at once.
    const std::string m(1000000, 'a');
    Sha256 h;
    size_t p = 0, step = 1;
    while (p < m.size()) {
        const size_t n = std::min(step, m.size() - p);
        h.update(m.data() + p, n);
        p += n;
        step = step * 3 % 997 + 1;
    }
    UCHECK(h.finishHex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    const fs::path f = fs::temp_directory_path() / "gh_sha_test.bin";
    put(f, "abc");
    std::string fileHex;
    UCHECK(sha256File(str(f), fileHex) && fileHex == hex("abc"));
    UCHECK(!sha256File(str(f) + ".none", fileHex));
    std::error_code ec;
    fs::remove(f, ec);
}

void testVersions() {
    using update::compareVersions;
    using update::parseVersion;
    UCHECK(parseVersion("v2.2.0").ok && parseVersion("v2.2.0").part[1] == 2);
    UCHECK(!parseVersion("dev").ok && !parseVersion("").ok);
    UCHECK(compareVersions(parseVersion("v2.10.0"), parseVersion("2.9.5")) > 0);
    UCHECK(compareVersions(parseVersion("2.2"), parseVersion("v2.2.0")) == 0);
    UCHECK(compareVersions(parseVersion("v2.1.0"), parseVersion("2.2.0")) < 0);
    UCHECK(compareVersions(parseVersion("V3.0.0-beta"), parseVersion("2.99.99")) > 0);
    // This build has a file on the releases, named as the release workflow names them.
    const std::string a = update::assetName();
    UCHECK(a.empty() || a.rfind("GodsHand-", 0) == 0);
}

void testReleaseJson() {
    // GitHub's answer for releases/latest, cut down (the fields the game reads, and others around them).
    const std::string json = R"({"tag_name":"v2.1.0","name":"God's Hand v2.1.0","draft":false,"prerelease":false,
      "body":"| Download | System |\n|---|---|\n> notes",
      "assets":[{"id":1,"name":"GodsHand-android.apk","size":35273389,"uploader":{"login":"github-actions[bot]"},
        "browser_download_url":"https://github.com/o/r/releases/download/v2.1.0/GodsHand-android.apk",
        "digest":"sha256:ED735AE654AA973B6667DEA285482A5EF032E1ACD97932346140ACBB3D909221"},
       {"id":2,"name":"GodsHand-linux-x64.zip","size":29482739,
        "browser_download_url":"https://github.com/o/r/releases/download/v2.1.0/GodsHand-linux-x64.zip",
        "digest":null},
       {"id":3,"name":"broken","size":1}]})";
    update::Release r;
    std::string err;
    UCHECK(update::parseRelease(json, r, &err));
    UCHECK(r.tag == "v2.1.0" && !r.draft && !r.prerelease && r.body.find("> notes") != std::string::npos);
    UCHECK(r.assets.size() == 2);  // the one with no address is left out
    const update::Asset* apk = r.find("GodsHand-android.apk");
    UCHECK(apk && apk->size == 35273389 && apk->sha256 == "ed735ae654aa973b6667dea285482a5ef032e1acd97932346140acbb3d909221");
    const update::Asset* lin = r.find("GodsHand-linux-x64.zip");
    UCHECK(lin && lin->sha256.empty() && lin->url.find("/v2.1.0/") != std::string::npos);
    UCHECK(!r.find("GodsHand-macos-arm64.zip"));
    UCHECK(!update::parseRelease("{\"message\":\"API rate limit exceeded\"}", r, &err));
    UCHECK(!update::parseRelease("not json", r, &err));
}

void testStageAndApply() {
    const fs::path root = fs::temp_directory_path() / "gh_update_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path game = root / "GodsHand";
    const std::string exe = update::exeName();
    put(game / exe, "old program");
    put(game / "assets" / "a.txt", "old a");
    put(game / "mods" / "mine" / "song.json", "the player's mod");
    put(game / "settings.cfg", "volume=1");
    const fs::path zip = root / "pkg.zip";
    makeZip(zip, {{"GodsHand/" + exe, "new program"},
                  {"GodsHand/assets/a.txt", "new a"},
                  {"GodsHand/assets/sub/b.txt", "new b"},
                  {"GodsHand/mods/mine/song.json", "overwritten?"},
                  {"GodsHand/lib/libraylib.a", "for programmers"},
                  {"GodsHand/include/raylib.h", "for programmers"},
                  {"GodsHand/../evil.txt", "outside"}});
    const std::string base = str(game) + "/";
    std::vector<std::string> files;
    std::string err;
    UCHECK(update::stagePackage(str(zip), base + "update/staging", files, err));
    UCHECK(files.size() == 3);  // the program, a.txt and sub/b.txt: not mods/, lib/, include/ or ../
    UCHECK(update::writeReady(base, "v9.9.9", files, err));
    UCHECK(update::pendingVersion(base) == "v9.9.9");
    // Nothing moved yet: the files take their places at the next start.
    UCHECK(get(game / exe) == "old program" && get(game / "assets" / "a.txt") == "old a");
    const update::Applied ap = update::applyPending(base);
    UCHECK(ap.done && ap.exeChanged && ap.files == 3 && ap.failed == 0 && ap.version == "v9.9.9");
    UCHECK(get(game / exe) == "new program" && get(game / "assets" / "a.txt") == "new a");
    UCHECK(get(game / "assets" / "sub" / "b.txt") == "new b");
    UCHECK(get(game / "mods" / "mine" / "song.json") == "the player's mod" && get(game / "settings.cfg") == "volume=1");
    UCHECK(!fs::exists(game / "lib") && !fs::exists(game / "include") && !fs::exists(root / "evil.txt"));
    UCHECK(!fs::exists(game / "update" / "ready.txt") && !fs::exists(game / "update" / "staging"));
    UCHECK(update::takeAppliedNote(base) == "Updated to God's Hand v9.9.9." && update::takeAppliedNote(base).empty());
    UCHECK(update::pendingVersion(base).empty() && !update::applyPending(base).done);  // once only
    // The replaced files went to .old; they go once nothing uses them.
    update::removeOldFiles(base);
    UCHECK(!fs::exists(game / (exe + ".old")) && !fs::exists(game / "assets" / "a.txt.old"));
    UCHECK(!fs::exists(game / "update" / "old-files.txt"));
#if !defined(_WIN32)
    // The program can still be run (the zip's permissions, or rwx when it says none).
    UCHECK((fs::status(game / exe).permissions() & fs::perms::owner_exec) != fs::perms::none);
#endif
    // Something that is not the game's package is refused.
    makeZip(root / "other.zip", {{"Other/readme.txt", "x"}});
    UCHECK(!update::stagePackage(str(root / "other.zip"), base + "update/staging", files, err) && !err.empty());
    UCHECK(!fs::exists(game / "update" / "staging"));
    fs::remove_all(root, ec);
}

void testZipDirectory() {
    const std::vector<std::pair<std::string, std::string>> files = {
        {"Mod/data/s1/s1.json", "{}"}, {"Mod/images/x.png", std::string(5000, 'p')}, {"Mod/pack.json", "{\"n\":1}"}};
    for (const bool z64 : {false, true}) {
        const std::string bytes = zipBytes(files, z64);
        std::vector<modsync::ZipEntry> all, part;
        uint64_t off = 0, size = 0;
        // The whole file as its tail.
        UCHECK(modsync::parseCentralDirectory(bytes, bytes.size(), all, off, size) == modsync::Cd::Ok);
        UCHECK(all.size() == 3);
        std::map<std::string, modsync::ZipEntry> byName;
        for (const auto& e : all) byName[e.name] = e;
        UCHECK(byName.count("Mod/images/x.png") && byName["Mod/images/x.png"].size == 5000);
        UCHECK(byName["Mod/pack.json"].crc == uint32_t(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>("{\"n\":1}"), 7)));
        // A tail too short for the directory: where it is, then the directory itself.
        const std::string tail = bytes.substr(bytes.size() - (z64 ? 120 : 40));
        UCHECK(modsync::parseCentralDirectory(tail, bytes.size(), part, off, size) == modsync::Cd::NeedMore);
        UCHECK(off + size <= bytes.size() && modsync::parseCentralDirectoryAt(bytes.substr(size_t(off), size_t(size)), part));
        UCHECK(part.size() == 3 && part[1].name == all[1].name && part[1].crc == all[1].crc);
        UCHECK(modsync::parseCentralDirectory("not a zip at all, long enough to look", 37, part, off, size) == modsync::Cd::Bad);
    }
    // Where a zip's files go: as installModZips always put them.
    UCHECK(modsync::layoutOf({"Cool Mod/", "Cool Mod/data/a.json"}, "cool-v2").folder == "Cool Mod");
    UCHECK(modsync::layoutOf({"mods/X/a.txt", "mods/X/b/c.txt"}, "z").prefix == "mods/X/");
    UCHECK(modsync::layoutOf({"data/a/a.json", "images/b.png"}, "loose").folder == "loose");
    UCHECK(modsync::layoutOf({"data/a/a.json"}, "loose").folder == "loose");  // "data" is a mod's contents
    UCHECK(modsync::layoutOf({"readme.txt", "M/a.txt"}, "zz").folder == "zz");
}

void testModChanges() {
    const fs::path root = fs::temp_directory_path() / "gh_modsync_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const std::string mods = str(root / "mods") + "/";
    fs::create_directories(root / "mods");
    const fs::path v1 = root / "dl" / "GodsHand-mod-pack.zip";
    makeZip(v1, {{"Pack/data/s1/s1.json", "chart 1"},
                 {"Pack/songs/s1/Inst.ogg", "inst 1"},
                 {"Pack/images/a.png", "a v1"},
                 {"Pack/images/b.png", "b v1"},
                 {"Pack/../evil.txt", "x"}});
    modsync::InstallOptions opt;
    opt.wholePack = true;
    opt.source = "pack";
    opt.zipInfo = {{"GodsHand-mod-pack.zip", "aaaa", 10}};
    modsync::InstallResult res;
    UCHECK(modsync::installZips(mods, {str(v1)}, opt, res));
    UCHECK(res.folder == "Pack" && res.written == 4 && res.skipped == 1 && res.message.find("skipped") != std::string::npos);
    UCHECK(get(root / "mods" / "Pack" / "images" / "a.png") == "a v1" && !fs::exists(root / "evil.txt"));
    modsync::Record rec;
    UCHECK(modsync::loadRecord(mods, "Pack", rec) && rec.source == "pack" && rec.files.size() == 4);
    UCHECK(rec.zips.size() == 1 && rec.zips[0].sha256 == "aaaa");
    UCHECK(modsync::recordedFolders(mods) == std::vector<std::string>{"Pack"});
    put(root / "mods" / "Pack" / "mine.txt", "the player's own file");

    // The pack's new version: a.png changed, b.png and song s1 gone, song s2 added.
    const fs::path v2 = root / "dl2" / "GodsHand-mod-pack.zip";
    makeZip(v2, {{"Pack/data/s2/s2.json", "chart 2"}, {"Pack/songs/s2/Inst.ogg", "inst 2"}, {"Pack/images/a.png", "a v2"}});
    opt.zipInfo = {{"GodsHand-mod-pack.zip", "bbbb", 20}};
    UCHECK(modsync::installZips(mods, {str(v2)}, opt, res));
    UCHECK(res.diff.songsAdded == std::vector<std::string>{"s2"} && res.diff.songsRemoved == std::vector<std::string>{"s1"});
    UCHECK(res.diff.changed == std::vector<std::string>{"images/a.png"} && res.diff.removed.size() == 3 && res.diff.added.size() == 2);
    UCHECK(res.diff.summary().find("1 song added") != std::string::npos);
    UCHECK(get(root / "mods" / "Pack" / "images" / "a.png") == "a v2" && !fs::exists(root / "mods" / "Pack" / "images" / "b.png"));
    UCHECK(!fs::exists(root / "mods" / "Pack" / "songs" / "s1") && fs::exists(root / "mods" / "Pack" / "songs" / "s2" / "Inst.ogg"));
    UCHECK(get(root / "mods" / "Pack" / "mine.txt") == "the player's own file");
    modsync::UndoInfo u = modsync::undoInfo(mods, "Pack");
    UCHECK(u.exists && u.when > 0 && u.summary == res.diff.summary());

    // Remove changes: the first version back, exactly.
    std::string msg;
    UCHECK(modsync::undoChanges(mods, "Pack", msg));
    UCHECK(get(root / "mods" / "Pack" / "images" / "a.png") == "a v1" && get(root / "mods" / "Pack" / "images" / "b.png") == "b v1");
    UCHECK(get(root / "mods" / "Pack" / "songs" / "s1" / "Inst.ogg") == "inst 1" && !fs::exists(root / "mods" / "Pack" / "songs" / "s2"));
    UCHECK(get(root / "mods" / "Pack" / "mine.txt") == "the player's own file");
    UCHECK(modsync::loadRecord(mods, "Pack", rec) && rec.files.size() == 4 && rec.zips[0].sha256 == "aaaa");
    UCHECK(!modsync::undoInfo(mods, "Pack").exists);

    // The same version again: nothing to write, the last change kept as it was (none now).
    opt.zipInfo = {{"GodsHand-mod-pack.zip", "aaaa", 10}};
    UCHECK(modsync::installZips(mods, {str(v1)}, opt, res) && res.written == 0 && res.diff.empty());

    // Remove pack: only what its record lists.
    UCHECK(modsync::removeMod(mods, "Pack", msg));
    UCHECK(!fs::exists(root / "mods" / "Pack" / "images") && !fs::exists(root / "mods" / "Pack" / "data"));
    UCHECK(get(root / "mods" / "Pack" / "mine.txt") == "the player's own file");
    UCHECK(!modsync::loadRecord(mods, "Pack", rec) && !modsync::removeMod(mods, "Pack", msg));

    // A zip dropped into mods/: a newer copy (the same name) removes what it no
    // longer has; another zip into the same mod (a patch) removes nothing.
    makeZip(root / "drop" / "loose.zip", {{"data/x/x.json", "x1"}, {"images/y.png", "y1"}});
    UCHECK(modsync::installZips(mods, {str(root / "drop" / "loose.zip")}, modsync::InstallOptions{}, res) && res.folder == "loose");
    makeZip(root / "drop2" / "loose.zip", {{"data/x/x.json", "x2"}});
    UCHECK(modsync::installZips(mods, {str(root / "drop2" / "loose.zip")}, modsync::InstallOptions{}, res));
    UCHECK(get(root / "mods" / "loose" / "data" / "x" / "x.json") == "x2" && !fs::exists(root / "mods" / "loose" / "images" / "y.png"));
    makeZip(root / "drop3" / "loose.zip", {{"images/z.png", "z"}});
    // (named as the folder holds it, but its own zip: a patch)
    fs::rename(root / "drop3" / "loose.zip", root / "drop3" / "patch.zip");
    makeZip(root / "drop3" / "patch.zip", {{"loose/images/z.png", "z"}});
    UCHECK(modsync::installZips(mods, {str(root / "drop3" / "patch.zip")}, modsync::InstallOptions{}, res) && res.folder == "loose");
    UCHECK(fs::exists(root / "mods" / "loose" / "data" / "x" / "x.json") && get(root / "mods" / "loose" / "images" / "z.png") == "z");
    UCHECK(modsync::loadRecord(mods, "loose", rec) && rec.zips.size() == 2 && rec.files.size() == 2);
    fs::remove_all(root, ec);
}

void testPackCompare() {
    const fs::path root = fs::temp_directory_path() / "gh_packcmp_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const std::string mods = str(root / "mods") + "/";
    fs::create_directories(root / "mods");
    // The release: pack A (unchanged), pack B (changed), pack C (not installed); D was taken off it.
    std::map<std::string, std::string> remote;
    remote["GodsHand-mod-a.zip"] = zipBytes({{"A/data/s/s.json", "a"}});
    remote["GodsHand-mod-b.zip"] = zipBytes({{"B/data/n/n.json", "new"}, {"B/images/i.png", "i2"}});
    remote["GodsHand-mod-c-part1.zip"] = zipBytes({{"C/x.txt", "c"}});
    auto shaOf = [](const std::string& s) {
        Sha256 h;
        h.update(s.data(), s.size());
        return h.finishHex();
    };
    auto install = [&](const std::string& name, const std::string& bytes, const std::string& sha) {
        const fs::path z = root / "dl" / name;
        fs::create_directories(z.parent_path());
        std::ofstream(z, std::ios::binary) << bytes;
        modsync::InstallOptions opt;
        opt.wholePack = true;
        opt.source = "pack";
        opt.zipInfo = {{name, sha, bytes.size()}};
        modsync::InstallResult res;
        return modsync::installZips(mods, {str(z)}, opt, res);
    };
    UCHECK(install("GodsHand-mod-a.zip", remote["GodsHand-mod-a.zip"], shaOf(remote["GodsHand-mod-a.zip"])));
    const std::string oldB = zipBytes({{"B/data/o/o.json", "old"}, {"B/images/i.png", "i1"}});
    UCHECK(install("GodsHand-mod-b.zip", oldB, shaOf(oldB)));
    UCHECK(install("GodsHand-mod-d.zip", zipBytes({{"D/d.txt", "d"}}), "dddd"));
    // E: installed by an older game (no record), the same files as the release.
    remote["GodsHand-mod-e.zip"] = zipBytes({{"E/e.txt", "e"}});
    put(root / "mods" / "E" / "e.txt", "e");

    std::string api = R"({"tag_name":"mods","assets":[)";
    bool first = true;
    for (const auto& [name, bytes] : remote) {
        api += std::string(first ? "" : ",") + R"({"name":")" + name + R"(","size":)" + std::to_string(bytes.size()) +
               R"(,"browser_download_url":"https://example.invalid/)" + name + R"(","digest":"sha256:)" + shaOf(bytes) + "\"}";
        first = false;
    }
    api += "]}";
    modsync::Manifest man;
    UCHECK(modsync::parseManifest("A\tGodsHand-mod-a.zip\nB\tGodsHand-mod-b.zip\nC\tGodsHand-mod-c-part1.zip\n"
                                  "E\tGodsHand-mod-e.zip\n../bad\tx.zip\nnozips\t\n",
                                  man));
    UCHECK(man.packs.size() == 4);
    int fetches = 0;
    const modsync::FetchRange fetch = [&](const std::string& url, const std::string& range, std::string& bytes, std::string& err) {
        ++fetches;
        const std::string name = url.substr(url.find_last_of('/') + 1);
        if (!remote.count(name)) {
            err = "404";
            return false;
        }
        const std::string& b = remote[name];
        if (range[0] == '-') {
            const size_t n = std::min<size_t>(std::stoull(range.substr(1)), b.size());
            bytes = b.substr(b.size() - n);
        } else {
            const size_t dash = range.find('-');
            const size_t a = std::stoull(range.substr(0, dash)), e = std::stoull(range.substr(dash + 1));
            bytes = b.substr(a, e - a + 1);
        }
        return true;
    };
    const std::vector<modsync::PackStatus> st = modsync::comparePacks(mods, api, man, fetch);
    std::map<std::string, modsync::PackStatus> by;
    for (const auto& s : st) by[s.folder] = s;
    UCHECK(st.size() == 5);
    UCHECK(by["A"].kind == modsync::PackStatus::UpToDate && !by["A"].diffKnown);  // the API's SHA-256 was enough
    UCHECK(by["B"].kind == modsync::PackStatus::Changed && by["B"].diffKnown);
    UCHECK(by["B"].diff.songsAdded == std::vector<std::string>{"n"} && by["B"].diff.songsRemoved == std::vector<std::string>{"o"});
    UCHECK(by["B"].diff.changed == std::vector<std::string>{"images/i.png"});
    UCHECK(by["C"].kind == modsync::PackStatus::NotInstalled && by["C"].zips.size() == 1 && !by["C"].zips[0].sha256.empty());
    UCHECK(by["D"].kind == modsync::PackStatus::Gone);
    UCHECK(by["E"].kind == modsync::PackStatus::UpToDate && by["E"].diffKnown);
    UCHECK(fetches == 2);  // the ends of B's and E's zips only
    modsync::Record rec;
    UCHECK(modsync::loadRecord(mods, "E", rec) && rec.source == "pack" && rec.files.size() == 1);  // remembered for next time
    // Next time E needs no fetch either.
    fetches = 0;
    modsync::comparePacks(mods, api, man, fetch);
    UCHECK(fetches == 1);  // B only (still changed)
    // Without byte ranges a changed pack cannot be read: it is not called up to date.
    const std::vector<modsync::PackStatus> st2 = modsync::comparePacks(mods, api, man, {});
    for (const auto& s : st2)
        if (s.folder == "B") UCHECK(s.kind == modsync::PackStatus::Unknown);
    fs::remove_all(root, ec);
}

}  // namespace

void runUpdateTests() {
    testSha256();
    testVersions();
    testReleaseJson();
    testStageAndApply();
    testZipDirectory();
    testModChanges();
    testPackCompare();
}
