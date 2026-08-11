import { existsSync, lstatSync, mkdirSync, rmSync } from "node:fs";
import path from "node:path";

export function assertWithinRoot(root, target) {
  const relative = path.relative(path.resolve(root), path.resolve(target));
  if (!relative || relative.startsWith("..") || path.isAbsolute(relative)) {
    throw new Error(`managed path escaped root: ${target}`);
  }
}

export function assertNoReparseComponents(root, target) {
  assertWithinRoot(root, target);
  const relative = path.relative(path.resolve(root), path.resolve(target));
  let current = path.resolve(root);
  for (const component of relative.split(path.sep)) {
    current = path.join(current, component);
    if (!existsSync(current)) break;
    if (lstatSync(current).isSymbolicLink()) {
      throw new Error(`refusing managed path through symlink or junction: ${current}`);
    }
  }
}

export function resolveWithinRoot(root, relativePath) {
  const target = path.resolve(root, relativePath);
  assertWithinRoot(root, target);
  assertNoReparseComponents(root, target);
  return target;
}

export function resetManagedDirectory(root, directory) {
  assertWithinRoot(root, directory);
  assertNoReparseComponents(root, directory);
  if (existsSync(directory)) rmSync(directory, { recursive: true, force: false });
  mkdirSync(directory, { recursive: true });
}
