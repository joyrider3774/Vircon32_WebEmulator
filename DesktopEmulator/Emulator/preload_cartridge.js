// Pre-run hook (added via em++'s --pre-js) that fetches the cartridge ROM
// from the same directory as this page, instead of it being baked into the
// build's --preload-file package. This lets a ROM be swapped out just by
// replacing game.v32 next to the built .html/.js/.wasm files - no rebuild
// needed. The BIOS is still baked in via --preload-file (it rarely changes
// and is small), only the cartridge is fetched at runtime.
//
// An optional "?rom=someOtherName.v32" query parameter on the page's own
// URL overrides the default filename, so more than one ROM can be hosted
// side by side without separate builds.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  addRunDependency('fetch-cartridge');

  var romFileName = new URLSearchParams(window.location.search).get('rom') || 'game.v32';

  fetch(romFileName)
    .then(function (response) {
      if (!response.ok)
        throw new Error('Failed to fetch ' + romFileName + ': HTTP ' + response.status);
      return response.arrayBuffer();
    })
    .then(function (buffer) {
      FS.createPath('/', 'data', true, true);
      FS.writeFile('/data/cartridge.v32', new Uint8Array(buffer));
    })
    .catch(function (err) {
      // Still let the app start: Console.LoadCartridge() will throw its
      // own clear "cannot open file" error in the on-page log/console
      // instead of the page hanging forever on a missing run dependency.
      console.error(err);
    })
    .finally(function () {
      removeRunDependency('fetch-cartridge');
    });
});
