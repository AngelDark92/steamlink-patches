"""Read retained paths after the 2026-10-08 build relocation without rewriting receipts."""
from pathlib import Path


def relocate_artifact_path(path: str, repository: Path) -> str:
    repository = repository.resolve()
    candidate = Path(path).resolve()
    external = (repository / "../builds/steamlink-patches").resolve()
    for old, new in (("patches/build", "gradle/patches"), ("build", "build")):
        old_root = repository / old
        if candidate.is_relative_to(old_root):
            return str(external / new / candidate.relative_to(old_root))
    return str(candidate)


def relocate_classpath(classpath: str, repository: Path) -> str:
    # These retained compiler receipts use Windows/Java semicolon classpaths.
    return ";".join(relocate_artifact_path(item, repository)
                    for item in classpath.strip().split(";") if item)
