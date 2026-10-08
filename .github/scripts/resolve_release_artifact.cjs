const fs = require('node:fs');
const path = require('node:path');

function resolveReleaseArtifact(workspace, version) {
  if (!version || !/^[0-9A-Za-z.+-]+$/.test(version)) {
    throw new Error('RELEASE_VERSION must identify a single release version');
  }
  // Attestation rejects every '..' segment, even in an absolute pattern.
  const artifact = fs.realpathSync(path.resolve(
    workspace, '..', 'builds', 'steamlink-patches', 'gradle', 'patches', 'libs',
    `patches-${version}.mpp`,
  ));
  if (!fs.statSync(artifact).isFile()) {
    throw new Error(`Released bundle is not a file: ${artifact}`);
  }
  return artifact;
}

if (require.main === module) {
  const artifact = resolveReleaseArtifact(process.cwd(), process.env.RELEASE_VERSION);
  fs.appendFileSync(process.env.GITHUB_OUTPUT, `path=${artifact}\n`);
}

module.exports = { resolveReleaseArtifact };
