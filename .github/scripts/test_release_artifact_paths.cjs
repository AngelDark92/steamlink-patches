const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { resolveReleaseArtifact } = require('./resolve_release_artifact.cjs');

async function main() {
  const glob = await import('@actions/glob');
  const outputRoot = path.resolve(__dirname, '../../../builds/steamlink-patches/validation');
  fs.mkdirSync(outputRoot, { recursive: true });
  const fixture = fs.mkdtempSync(path.join(outputRoot, 'release-paths-'));
  const workspace = path.join(fixture, 'checkout');
  const libs = path.join(fixture, 'builds/steamlink-patches/gradle/patches/libs');
  fs.mkdirSync(workspace);
  fs.mkdirSync(libs, { recursive: true });
  fs.writeFileSync(path.join(libs, 'patches-1.27.0.mpp'), 'old preflight bundle');
  const released = path.join(libs, 'patches-1.28.0-dev.2.mpp');
  fs.writeFileSync(released, 'new release bundle');
  const canonical = resolveReleaseArtifact(workspace, '1.28.0-dev.2');
  assert.equal(canonical, fs.realpathSync(released));
  assert.deepEqual(await (await glob.create(canonical)).glob(), [canonical]);
  await assert.rejects(glob.create('../builds/steamlink-patches/gradle/patches/libs/patches-*.mpp'), /Relative pathing/);
  assert.throws(() => resolveReleaseArtifact(workspace, '9.9.9'), /ENOENT/);
  assert.throws(() => resolveReleaseArtifact(workspace, ''), /RELEASE_VERSION/);
  assert.throws(() => resolveReleaseArtifact(workspace, '../1.28.0'), /RELEASE_VERSION/);
  fs.mkdirSync(path.join(libs, 'patches-2.0.0.mpp'));
  assert.throws(() => resolveReleaseArtifact(workspace, '2.0.0'), /not a file/);

  // Exercise the workflow entry point and its GitHub output, not only the export.
  const githubOutput = path.join(fixture, 'github-output');
  const result = spawnSync(process.execPath, [path.join(__dirname, 'resolve_release_artifact.cjs')], {
    cwd: workspace,
    env: { ...process.env, RELEASE_VERSION: '1.28.0-dev.2', GITHUB_OUTPUT: githubOutput },
    encoding: 'utf8',
  });
  assert.equal(result.status, 0, result.stderr);
  assert.equal(fs.readFileSync(githubOutput, 'utf8'), `path=${canonical}\n`);
  console.log('Release artifact paths: 9 assertions passed (actual @actions/glob).');
  console.log(`Fixtures retained outside source: ${fixture}`);
}

main().catch(error => { console.error(error); process.exitCode = 1; });
