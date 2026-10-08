// Mounts a persistent (IndexedDB-backed) directory so memory card saves
// survive page reloads and browser restarts. Everything else in the
// Emscripten virtual filesystem (the BIOS, the fetched ROM) is plain
// in-memory MEMFS and simply vanishes when the tab closes - which is
// fine for those, since they're re-fetched/re-mounted fresh on every
// page load anyway. IDBFS does NOT sync automatically on every write (it
// would be far too slow to hit IndexedDB on every single file write), so
// pushing changes to IndexedDB is a separate, explicit step - see
// Module.persistMemoryCard() below, called from MainWeb.cpp whenever the
// memory card was actually modified and saved to the in-memory copy.
//
// The mount path is derived from location.pathname rather than a fixed
// string, and exposed as Module.persistMountPath for MainWeb.cpp to read
// at startup (see GetPersistMountPathJS() there). IndexedDB storage is
// scoped per *origin*, not per path - confirmed directly: two different
// game deployments under the same domain but different subdirectories
// (e.g. mygames.com/gameA/ and mygames.com/gameB/, a completely normal
// itch.io-style hosting pattern, especially since every build here is
// named the same generic Vircon32Web.* regardless of which game it is)
// otherwise end up sharing one single IndexedDB database, and with it
// each other's memory card directory contents. A fixed mount path would
// only be as safe as the memory card filename scheme in MainWeb.cpp
// happening to never collide (it's keyed off the cartridge's own title,
// which is not guaranteed unique across separate deployments - e.g. the
// same game hosted twice). Deriving the mount path itself from the
// page's path means each deployment gets a genuinely separate IndexedDB
// database - real isolation, not just a naming convention on top of
// shared storage.
var PersistMountPath = '/persist_' + location.pathname.replace(/[^a-zA-Z0-9]/g, '_');
Module['persistMountPath'] = PersistMountPath;

Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  addRunDependency('mount-persistent-storage');

  FS.mkdir(PersistMountPath);
  FS.mount(IDBFS, {}, PersistMountPath);

  // pull in whatever was saved in a previous session, before main() (and
  // therefore any memory card load) is allowed to run
  FS.syncfs(true, function (err) {
    if (err) console.error('Failed to load persisted storage:', err);
    removeRunDependency('mount-persistent-storage');
  });
});

Module['persistMemoryCard'] = function () {
  FS.syncfs(false, function (err) {
    if (err) console.error('Failed to persist memory card:', err);
  });
};

// Best-effort only: there's no guarantee this IndexedDB write completes
// before the page actually unloads. The periodic save in MainWeb.cpp's
// main loop is what actually keeps data loss to a few seconds' worth in
// the worst case - this is just an extra attempt to catch anything since
// the last periodic save.
window.addEventListener('pagehide', function () {
  if (Module['persistMemoryCard']) Module['persistMemoryCard']();
});
